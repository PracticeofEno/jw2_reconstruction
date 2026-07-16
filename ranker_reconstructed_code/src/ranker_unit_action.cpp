#include "ranker_unit_action.h"

#include "ranker_gameplay_frame_render.h"
#include "ranker_gameplay_sound.h"
#include "ranker_gameplay_visibility.h"
#include "ranker_player_slots.h"
#include "ranker_runtime_resources.h"
#include "ranker_sprite_renderer.h"
#include "ranker_unit_commands.h"
#include "ranker_unit_damage.h"
#include "ranker_unit_lifecycle.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace ranker {
namespace {

const ProductionOrderRuntimeState& production_state_or_empty(
    const UnitActionContext& context) {
    static const ProductionOrderRuntimeState empty_state{};
    return context.production_state != nullptr ? *context.production_state : empty_state;
}

const ProductionOrderRuntimeState& production_state_or_empty(
    const UnitEffectRuntimeState& state) {
    static const ProductionOrderRuntimeState empty_state{};
    return state.production_state != nullptr ? *state.production_state : empty_state;
}

constexpr std::size_t kUnitEffectHitHistoryCapacity = 0x10;
constexpr std::array<UnitMovementPoint, 9> kUnitEffectProjectileStepByDirection{{
    {0, 0},
    {0, -2},
    {2, -2},
    {2, 0},
    {2, 2},
    {0, 2},
    {-2, 2},
    {-2, 0},
    {-2, -2},
}};
constexpr std::array<u32, 8> kUnitEffectSourceSortBiasByClass{
    0x50000000u, 0x50000000u, 0x50000000u, 0x80000000u,
    0x50000000u, 0, 0, 0};
constexpr std::array<u32, 8> kUnitEffectTargetImpactSortBiasByClass{
    0x70000000u, 0x70000000u, 0x70000000u, 0x90000000u,
    0x70000000u, 0, 0, 0};
constexpr u32 kUnitEffectPointImpactRuntimeSkipMask = 0x20000006u;
constexpr u32 kUnitEffectAreaRuntimeSkipMask = 0x20000086u;
constexpr u32 kUnitEffectAreaStunRuntimeSkipMask = 0x28000084u;
constexpr u32 kUnitEffectRepairRuntimeSkipMask = 400u;
constexpr u32 kUnitEffectAutoCommandLowSelectorMask = 0xffffu;
constexpr u32 kUnitEffectBehaviorBoundsImpact = 0x02u;
constexpr u32 kUnitEffectBehaviorSkipImpactCheck = 0x04u;
constexpr u32 kProjectileUnitImpactImageGroup = 3;
constexpr u32 kProjectileUnitImpactRowIndex = 1;

constexpr i32 action_center_coordinate(i32 origin, i32 bounds_start,
    i32 bounds_extent) {
    return origin + bounds_start + (bounds_extent >> 1);
}

i32 action_center_x(const UnitMovementUnit& unit) {
    return action_center_coordinate(unit.x, unit.definition.center_bounds_left,
        unit.definition.center_bounds_width);
}

i32 action_center_y(const UnitMovementUnit& unit) {
    return action_center_coordinate(unit.y, unit.definition.center_bounds_top,
        unit.definition.center_bounds_height);
}

UnitEffectRuntime* effect_slot_from_original_offset_allow_inactive(
    UnitEffectRuntimeState& state, u32 offset) {
    constexpr u32 kOriginalEffectStride = 0xa8u;
    if (offset == 0 || (offset % kOriginalEffectStride) != 0) {
        return nullptr;
    }
    const std::size_t index = offset / kOriginalEffectStride - 1u;
    return index < state.effect_slots.size()
        ? &state.effect_slots[index]
        : nullptr;
}

constexpr u32 action_direction_or_previous(u32 previous_direction,
    u32 calculated_direction) {
    return calculated_direction != 0 ? calculated_direction : previous_direction;
}

static_assert(action_center_coordinate(100, -4, 32) == 112);
static_assert(action_center_coordinate(200, 6, 18) == 215);
static_assert(action_direction_or_previous(7, 0) == 7);
static_assert(action_direction_or_previous(7, 3) == 3);

void update_action_direction_to_target_center(UnitActionContext& context,
    UnitMovementUnit& source, const UnitMovementUnit& target) {
    const UnitMovementPoint source_center{
        action_center_x(source), action_center_y(source)};
    const UnitMovementPoint target_center{
        action_center_x(target), action_center_y(target)};
    // CalculateUnitCenterPathDistance (FUN_004c32fb) forwards these centers to
    // PointDirectionLookupLowThunk.  In particular, the record-1 JW2_07.TRC
    // table owns slope-boundary cases such as delta (10, 20); the generic
    // movement heuristic returns a different direction at that boundary.
    u32 direction = 0;
    if (context.movement_context != nullptr &&
        context.movement_context->direction_lookup_8 != nullptr) {
        direction = CalculatePointDirectionFromLookup(source_center,
            target_center, *context.movement_context->direction_lookup_8);
    }
    else {
        UnitMovementUnit centered_source = source;
        centered_source.x = source_center.x;
        centered_source.y = source_center.y;
        direction = CalculateUnitDirectionToPoint(centered_source,
            target_center.x, target_center.y);
    }
    if (direction != 0) {
        source.direction = action_direction_or_previous(source.direction, direction);
    }
}

u32 default_action_distance(const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    return CalculateApproxUnitDistance(action_center_x(source), action_center_y(source),
        action_center_x(target), action_center_y(target));
}

UnitMovementPoint unit_effect_source_offset(const UnitMovementUnit& source) {
    const u32 direction = std::min<u32>(source.direction,
        static_cast<u32>(source.definition.effect_source_offset_by_direction.size() - 1));
    return source.definition.effect_source_offset_by_direction[direction];
}

u32 action_cycle_ticks(const UnitMovementUnit& source) {
    return std::max<u32>(source.definition.action_cycle_ticks, 1);
}

u32 action_range(UnitActionContext& context, const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    return CalculateUnitActionRangeWithProductionAndEquipmentEffects(
        production_state_or_empty(context), source, target.definition.render_class,
        context.equipment_catalog);
}

UnitActionTargetValidation default_target_reach_validation(
    UnitActionContext& context, const UnitMovementUnit& source,
    const UnitMovementUnit& target) {
    UnitActionTargetValidation result;
    result.valid = true;
    result.range = action_range(context, source, target);
    result.command_x = action_center_x(target);
    result.command_y = action_center_y(target);
    result.distance = context.callbacks.distance_to_target != nullptr
        ? context.callbacks.distance_to_target(context, source, target)
        : default_action_distance(source, target);
    result.in_range = result.distance <= result.range;
    return result;
}

bool can_replace_transient_target(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    if (context.callbacks.can_replace_transient_target != nullptr) {
        return context.callbacks.can_replace_transient_target(context, source, target);
    }
    return true;
}

const UnitEffectDefinition* find_effect_definition(
    const UnitEffectRuntimeState& state, u32 effect_id) {
    const auto it = std::find_if(state.definitions.begin(), state.definitions.end(),
        [effect_id](const UnitEffectDefinition& definition) {
            return definition.id == effect_id;
        });
    return it == state.definitions.end() ? nullptr : &*it;
}

UnitMovementUnit* find_effect_unit(UnitEffectRuntimeState& state, u32 unit_id) {
    if (unit_id == 0) {
        return nullptr;
    }
    for (UnitMovementUnit* unit : state.unit_refs) {
        if (unit != nullptr && unit->id == unit_id) {
            return unit;
        }
    }

    const auto it = std::find_if(state.units.begin(), state.units.end(),
        [unit_id](const UnitMovementUnit& unit) {
            return unit.id == unit_id;
        });
    if (it != state.units.end()) {
        return &*it;
    }

    // OBB raw +0x18/+0x1c references the physical record-7 pool, not merely
    // its active list.  Preserve active scan order above, then resolve legal
    // corpse/transition and stale free-node references by fixed-pool id.
    UnitMovementContext* movement = state.lifecycle_context != nullptr
        ? state.lifecycle_context->movement
        : nullptr;
    if (movement != nullptr) {
        for (UnitMovementUnit* unit : movement->lifecycle_units) {
            if (unit != nullptr && unit->id == unit_id) {
                return unit;
            }
        }
        for (UnitMovementUnit* unit : movement->free_units) {
            if (unit != nullptr && unit->id == unit_id) {
                return unit;
            }
        }
    }
    return nullptr;
}

const UnitMovementUnit* find_effect_unit(
    const UnitEffectRuntimeState& state, u32 unit_id) {
    if (unit_id == 0) {
        return nullptr;
    }
    for (const UnitMovementUnit* unit : state.unit_refs) {
        if (unit != nullptr && unit->id == unit_id) {
            return unit;
        }
    }

    const auto it = std::find_if(state.units.begin(), state.units.end(),
        [unit_id](const UnitMovementUnit& unit) {
            return unit.id == unit_id;
        });
    if (it != state.units.end()) {
        return &*it;
    }

    const UnitMovementContext* movement = state.lifecycle_context != nullptr
        ? state.lifecycle_context->movement
        : nullptr;
    if (movement != nullptr) {
        for (const UnitMovementUnit* unit : movement->lifecycle_units) {
            if (unit != nullptr && unit->id == unit_id) {
                return unit;
            }
        }
        for (const UnitMovementUnit* unit : movement->free_units) {
            if (unit != nullptr && unit->id == unit_id) {
                return unit;
            }
        }
    }
    return nullptr;
}

const UnitMovementDefinition* find_effect_unit_definition(
    UnitEffectRuntimeState& state, u32 type_id,
    const UnitMovementUnit* fallback = nullptr) {
    if (state.lifecycle_context != nullptr &&
        state.lifecycle_context->callbacks.find_definition != nullptr) {
        if (const UnitMovementDefinition* definition =
                state.lifecycle_context->callbacks.find_definition(
                    *state.lifecycle_context, type_id)) {
            return definition;
        }
    }
    if (fallback != nullptr && fallback->type_id == type_id) {
        return &fallback->definition;
    }
    for (const UnitMovementUnit* unit : state.unit_refs) {
        if (unit != nullptr && unit->type_id == type_id) {
            return &unit->definition;
        }
    }
    const auto it = std::find_if(state.units.begin(), state.units.end(),
        [type_id](const UnitMovementUnit& unit) {
            return unit.type_id == type_id;
        });
    return it == state.units.end() ? nullptr : &it->definition;
}

void return_effect_target_to_idle(UnitMovementUnit& target) {
    if (target.command_state == kUnitStateRuntimeIdleAcquire) {
        return;
    }
    target.command_state = kUnitStateRuntimeIdleAcquire;
    if (target.definition.animation_timer_period <= target.animation_frame) {
        target.animation_frame = 0;
    }
}

bool effect_target_is_revivable_corpse(const UnitMovementUnit* target) {
    return target != nullptr && target->path_target_x != 1 &&
        (target->runtime_flags & kUnitActionTargetTransient) != 0;
}

void register_effect_unit_ref(UnitEffectRuntimeState& state,
    UnitMovementUnit& unit) {
    if (std::find(state.unit_refs.begin(), state.unit_refs.end(), &unit) ==
        state.unit_refs.end()) {
        state.unit_refs.push_back(&unit);
    }
}

void reactivate_effect_target(UnitEffectRuntimeState& state,
    UnitMovementUnit& target) {
    UnitLifecycleContext* lifecycle = state.lifecycle_context;
    if (lifecycle != nullptr) {
        SetUnitFootprintOccupancyBits(*lifecycle, target);
        if (lifecycle->movement != nullptr) {
            HandleLifecycleUnitActiveListMove(*lifecycle->movement, target);
        }
        else {
            target.active = true;
        }
        if (target.owner_id < kUnitOwnerTypeCountOwners &&
            target.type_id < kUnitOwnerTypeCountTypes) {
            u32& count =
                lifecycle->owner_unit_type_counts[target.owner_id][target.type_id];
            count = static_cast<u8>(count + 1u);
        }
    }
    else {
        target.active = true;
    }
    register_effect_unit_ref(state, target);

    bool footprint_command_flag =
        (target.definition.footprint_flags & 2u) != 0;
    if (!footprint_command_flag && state.equipment_catalog != nullptr) {
        footprint_command_flag = CalculateUnitEquipmentCommandFlagModifier(
            target, *state.equipment_catalog) != 0;
    }
    if (footprint_command_flag) {
        target.command_flags |= 0x40u;
    }
}

u32 effect_growth_countdown(const UnitMovementUnit& target) {
    return target.definition.lifecycle_growth_period == 0
        ? 0
        : target.definition.lifecycle_growth_period - 1u;
}

void append_effect_event(UnitEffectRuntimeState& state, UnitEffectEventKind kind,
    const UnitEffectRuntime& effect, u32 target_id = 0, u32 value = 0,
    UnitEffectSoundSpatialKind sound_spatial =
        UnitEffectSoundSpatialKind::world_point,
    const UnitMovementUnit* sound_spatial_anchor = nullptr) {
    UnitEffectEvent event{};
    event.kind = kind;
    event.sound_spatial = sound_spatial;
    event.effect_id = effect.effect_id;
    event.unit_id = effect.source_unit_id;
    event.target_id = target_id;
    event.value = value;
    event.x = effect.x;
    event.y = effect.y;
    if (sound_spatial != UnitEffectSoundSpatialKind::world_point) {
        event.sound_spatial_position_valid = sound_spatial_anchor != nullptr;
        if (sound_spatial_anchor != nullptr) {
            // The original wrappers consume the raw unit pointer immediately.
            // Snapshot that call-time tile so later pool/list mutation cannot
            // change or suppress an already-published sound event.
            event.x = sound_spatial_anchor->x;
            event.y = sound_spatial_anchor->y;
        }
    }
    state.events.push_back(event);
}

void record_effect_hit_target(UnitEffectRuntime& effect, u32 target_id) {
    if (effect.hit_unit_ids.size() < kUnitEffectHitHistoryCapacity) {
        effect.hit_unit_ids.push_back(target_id);
    }
}

u32 absolute_delta(i32 a, i32 b) {
    const i64 delta = static_cast<i64>(a) - static_cast<i64>(b);
    const u64 value = delta < 0 ? static_cast<u64>(-delta) : static_cast<u64>(delta);
    return value > 0xffffffffull ? 0xffffffffu : static_cast<u32>(value);
}

u32 lookup_unit_effect_direction(UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect) {
    if (state.lifecycle_context != nullptr &&
        state.lifecycle_context->movement != nullptr &&
        state.lifecycle_context->movement->direction_lookup_8 != nullptr) {
        return CalculatePointDirectionFromLookup(
            UnitMovementPoint{effect.x, effect.y},
            UnitMovementPoint{effect.target_x, effect.target_y},
            *state.lifecycle_context->movement->direction_lookup_8);
    }
    return LookupUnitEffectDirection(effect);
}

void configure_projectile_axis_steps(UnitEffectRuntime& effect,
    i32 delta_x, i32 delta_y) {
    effect.step_x = delta_x < 0 ? -2 : (delta_x > 0 ? 2 : 0);
    effect.step_y = delta_y < 0 ? -2 : (delta_y > 0 ? 2 : 0);
    if (delta_x == 0 && delta_y == 0) {
        // Sign-quadrant selector zero indexes the original direction-1 step.
        effect.step_y = -2;
    }
}

void configure_projectile_step_fields(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    effect.direction = lookup_unit_effect_direction(state, effect);
    // The original 0x004f2d7b direction and step lookups are independent:
    // EAX selects the JW2_07 octant, while ECX selects a delta-sign quadrant.
    // Thus shallow horizontal/vertical octants still advance both non-zero
    // axes during the Bresenham minor-axis step.
    configure_projectile_axis_steps(effect, effect.delta_x, effect.delta_y);
    effect.abs_delta_x = absolute_delta(effect.target_x, effect.x);
    effect.abs_delta_y = absolute_delta(effect.target_y, effect.y);
    effect.accumulator_x = effect.abs_delta_x;
    effect.accumulator_y = effect.abs_delta_y;
    effect.flags &= ~kUnitEffectFlagProjectileYMajor;
    if (effect.abs_delta_x < effect.abs_delta_y) {
        effect.flags |= kUnitEffectFlagProjectileYMajor;
    }
}

void advance_unit_effect_projectile_step(UnitEffectRuntime& effect) {
    effect.previous_x = effect.x;
    effect.previous_y = effect.y;
    if ((effect.flags & kUnitEffectFlagProjectileYMajor) == 0) {
        effect.x += effect.step_x;
        effect.accumulator_y += effect.abs_delta_y;
        if (effect.abs_delta_x <= effect.accumulator_y) {
            effect.y += effect.step_y;
            effect.accumulator_y -= effect.abs_delta_x;
        }
    } else {
        effect.y += effect.step_y;
        effect.accumulator_x += effect.abs_delta_x;
        if (effect.abs_delta_y <= effect.accumulator_x) {
            effect.x += effect.step_x;
            effect.accumulator_x -= effect.abs_delta_y;
        }
    }
}

u32 active_frame_count(const UnitEffectDefinition* definition) {
    return definition != nullptr ? std::max<u32>(1, definition->active_frames) : 1;
}

u32 startup_timer_limit(const UnitEffectDefinition* definition) {
    if (definition == nullptr) {
        return 0;
    }
    return std::max<u32>(1, definition->startup_ticks);
}

u32 impact_render_ticks(const UnitEffectDefinition* definition) {
    return definition != nullptr ? std::max<u32>(1, definition->impact_render_ticks) : 1;
}

bool selected_action_effect_uses_projectile_path(
    const UnitEffectDefinition& definition) {
    return definition.action_path_control == 1 ||
        definition.action_projectile_loop_ticks != 0;
}

bool effect_uses_tick_animation_frame(const UnitEffectRuntime& effect) {
    if (effect.effect_id >= 0x3du) {
        return false;
    }
    // The ordinary low-id projectile dispatcher uses raw +0x0c as its
    // animation counter. Raw +0x10 is the remaining path budget while the
    // projectile is active. The target marker and the 0x20 afterimage clone
    // are specialized low-id paths that animate through `frame` instead.
    return effect.effect_id != 0x27u &&
        !(effect.effect_id == 0x20u &&
            (effect.flags & kUnitEffectFlagAfterimageClone) != 0);
}

u32 effect_animation_frame(const UnitEffectRuntime& effect) {
    return effect_uses_tick_animation_frame(effect) ? effect.tick : effect.frame;
}

u32 effect_frame_index(const UnitEffectRuntime& effect, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    u32 frame = effect_animation_frame(effect);
    if (frame == 0xffffffffu) {
        frame = 0;
    }
    return frame % static_cast<u32>(size);
}

bool effect_frame_in_range(const UnitEffectRuntime& effect, std::size_t size,
    std::size_t& index) {
    const u32 frame = effect_animation_frame(effect);
    if (frame == 0xffffffffu || static_cast<std::size_t>(frame) >= size) {
        return false;
    }
    index = static_cast<std::size_t>(frame);
    return true;
}

u32 effect_sprite_entry_for_frame(const UnitEffectRuntimeState& state,
    const UnitEffectDefinition& definition, const UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagImpact) != 0 &&
        !definition.impact_image_indices.empty() &&
        !definition.image_resource_entries.empty()) {
        std::size_t frame_index = 0;
        if (!effect_frame_in_range(
                effect, definition.impact_image_indices.size(), frame_index)) {
            return 0;
        }
        u32 image_index = definition.impact_image_indices[frame_index];
        if (const UnitMovementUnit* target =
                find_effect_unit(state, effect.target_unit_id)) {
            image_index += definition.impact_class_stride_factor *
                definition.impact_class_frame_count *
                target->definition.projectile_impact_class;
        }
        if (image_index < definition.image_resource_entries.size()) {
            return definition.image_resource_entries[image_index];
        }
    }

    const std::vector<u32>* sequence = nullptr;
    if ((effect.flags & kUnitEffectFlagImpact) != 0 &&
        !definition.impact_sprite_entries.empty()) {
        sequence = &definition.impact_sprite_entries;
    } else if ((effect.flags & 2u) != 0 &&
        !definition.startup_sprite_entries.empty()) {
        sequence = &definition.startup_sprite_entries;
    } else if (!definition.active_sprite_entries.empty()) {
        if (definition.directional_active_frames) {
            const std::size_t frame_stride =
                definition.active_sprite_entries.size() / 8;
            if (frame_stride == 0 || effect.direction == 0 ||
                effect.direction > 8) {
                return 0;
            }
            const u32 frame = effect_animation_frame(effect);
            if (frame == 0xffffffffu ||
                static_cast<std::size_t>(frame) >= frame_stride) {
                return 0;
            }
            const std::size_t index =
                static_cast<std::size_t>(effect.direction - 1) * frame_stride +
                static_cast<std::size_t>(frame);
            return index < definition.active_sprite_entries.size()
                ? definition.active_sprite_entries[index]
                : 0;
        }
        sequence = &definition.active_sprite_entries;
    } else if (!definition.image_resource_entries.empty()) {
        sequence = &definition.image_resource_entries;
    }
    if (sequence == nullptr || sequence->empty()) {
        return definition.sprite_entry;
    }

    std::size_t index = 0;
    if (sequence != &definition.image_resource_entries &&
        !effect_frame_in_range(effect, sequence->size(), index)) {
        return 0;
    }
    if (sequence == &definition.image_resource_entries) {
        index = effect_frame_index(effect, sequence->size());
    }
    return (*sequence)[index];
}

u32 effect_draw_mode_for_frame(const UnitEffectDefinition& definition,
    const UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        return definition.impact_draw_mode;
    }
    if ((effect.flags & 2u) != 0) {
        return definition.startup_draw_mode;
    }
    return definition.active_draw_mode;
}

u32 effect_source_sort_bias(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    const UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (source == nullptr) {
        return 0x10000000u;
    }
    const u32 class_index = std::min<u32>(
        source->definition.lifecycle_class,
        static_cast<u32>(kUnitEffectSourceSortBiasByClass.size() - 1));
    return kUnitEffectSourceSortBiasByClass[class_index];
}

u32 effect_special_source_sort_bias(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    if (effect.effect_id == 0x63 || effect.effect_id == 0x69) {
        return 0x90000000u;
    }
    return effect_source_sort_bias(state, effect);
}

u32 effect_target_impact_sort_bias(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    const UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
    if (target == nullptr) {
        return 0x90000000u;
    }
    const u32 class_index = std::min<u32>(
        target->definition.lifecycle_class,
        static_cast<u32>(kUnitEffectTargetImpactSortBiasByClass.size() - 1));
    return kUnitEffectTargetImpactSortBiasByClass[class_index];
}

u32 effect_indirect_render_sort_layer(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect,
    u32 handler) {
    switch (handler) {
    case 0:
        return effect_special_source_sort_bias(state, effect);
    case 1:
        return 0x90000000u;
    case 2:
        if (effect.target_unit_id == 0) {
            return 0x90000000u;
        }
        [[fallthrough]];
    case 3:
        if ((effect.flags & kUnitEffectFlagImpact) != 0) {
            return effect_target_impact_sort_bias(state, effect);
        }
        if (effect.flags == kUnitEffectFlagStartup) {
            return effect_special_source_sort_bias(state, effect);
        }
        return 0x90000000u;
    case 4:
        return 0x50000000u;
    default:
        return (effect.flags & kUnitEffectFlagImpact) != 0
            ? 0x50000000u
            : 0x10000000u;
    }
}

u32 effect_render_sort_layer(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    if (effect.effect_id == 0x20) {
        return 0x50000000u;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (effect.effect_id >= 0x3du) {
        const u32 projectile_index = effect.effect_id - 0x3du;
        if (projectile_index == 0x03 || projectile_index == 0x05 ||
            projectile_index == 0x16 || projectile_index == 0x1a) {
            if ((effect.flags & kUnitEffectFlagImpact) == 0 &&
                effect.flags == kUnitEffectFlagStartup) {
                return effect_special_source_sort_bias(state, effect);
            }
            return 0x10000000u;
        }
        if (projectile_index == 0x25) {
            return (effect.flags & kUnitEffectFlagImpact) != 0
                ? 0x50000000u
                : 0x10000000u;
        }
        if (definition != nullptr && definition->render_sort_handler != 0xffffffffu) {
            return effect_indirect_render_sort_layer(
                state, effect, definition->render_sort_handler);
        }
    } else if (definition != nullptr &&
        definition->render_sort_handler != 0xffffffffu) {
        return effect_indirect_render_sort_layer(
            state, effect, definition->render_sort_handler);
    }
    return (effect.flags & kUnitEffectFlagImpact) != 0
        ? 0x50000000u
        : 0x10000000u;
}

bool frame_in_list(const std::vector<u32>& frames, u32 frame) {
    return std::find(frames.begin(), frames.end(), frame) != frames.end();
}

std::size_t invalid_effect_slot_index() {
    return static_cast<std::size_t>(-1);
}

std::size_t effect_slot_index(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    for (std::size_t index = 0; index < state.effect_slots.size(); ++index) {
        if (&state.effect_slots[index] == &effect) {
            return index;
        }
    }
    return invalid_effect_slot_index();
}

bool has_effect_slot_index(const std::vector<std::size_t>& indices,
    std::size_t index) {
    return std::find(indices.begin(), indices.end(), index) != indices.end();
}

void remove_effect_slot_index(std::vector<std::size_t>& indices,
    std::size_t index) {
    indices.erase(std::remove(indices.begin(), indices.end(), index), indices.end());
}

void ensure_effect_slot_pool_initialized(UnitEffectRuntimeState& state) {
    if (!state.effect_slots.empty() || !state.free_effect_indices.empty() ||
        state.effect_slot_capacity == 0) {
        return;
    }
    state.effect_slots.resize(state.effect_slot_capacity);
    state.free_effect_indices.reserve(state.effect_slot_capacity);
    for (std::size_t index = 0; index < state.effect_slot_capacity; ++index) {
        state.free_effect_indices.push_back(index);
    }
}

void finish_effect(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    append_effect_event(state, UnitEffectEventKind::finished, effect);
    effect.active = false;
    effect.tick = 0;
    effect.frame = 0;
}

bool effect_inside_viewport(const UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect) {
    if (state.viewport_right <= state.viewport_left ||
        state.viewport_bottom <= state.viewport_top) {
        return true;
    }
    // FUN_004d7790 builds the effect cull rectangle 0x80 pixels beyond each
    // camera edge.  Keep the live camera origin itself unchanged because the
    // queued screen coordinates and trail endpoints still use that origin.
    const i64 x = effect.x;
    const i64 y = effect.y;
    return x >= static_cast<i64>(state.viewport_left) - 0x80 &&
        x < static_cast<i64>(state.viewport_right) + 0x80 &&
        y >= static_cast<i64>(state.viewport_top) - 0x80 &&
        y < static_cast<i64>(state.viewport_bottom) + 0x80;
}

bool effect_visible_on_current_grid(const UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect) {
    const GameplayVisibilityGrid* grid = state.visibility_grid;
    if (grid == nullptr || grid->width == 0 || grid->height == 0 ||
        grid->current.empty()) {
        return true;
    }
    if (effect.x < 0 || effect.y < 0) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(effect.x) >> 5;
    const u32 tile_y = static_cast<u32>(effect.y) >> 5;
    if (tile_x >= grid->width || tile_y >= grid->height) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(tile_y) * grid->width + tile_x;
    if (index >= grid->current.size()) {
        return false;
    }
    const u32 cell = grid->current[index];
    if (state.require_revealed_visibility &&
        (cell & kGameplayVisibilityRevealed) == 0) {
        return false;
    }
    return (cell & kGameplayVisibilityVisible) != 0;
}

bool unit_effect_low_id_sprite_blit_allowed(
    const UnitEffectDefinition& definition, const UnitEffectRuntime& effect) {
    if (effect.effect_id >= 0x3du) {
        return true;
    }
    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        return effect.tick < definition.impact_render_ticks;
    }
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        return effect.tick < definition.startup_ticks;
    }
    if (definition.projectile) {
        return definition.render_ticks != 0;
    }
    return effect.tick < definition.render_ticks;
}

bool queue_effect_render_command(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (state.render_queue == nullptr || !effect.active ||
        !effect_inside_viewport(state, effect)) {
        return false;
    }

    const u32 layer = effect_render_sort_layer(state, effect);
    const u32 packed_y = static_cast<u32>(effect.y + 1);
    const u32 packed_x = static_cast<u32>(effect.x);

    GameplayRenderCommand command{};
    command.class_id = 9;
    command.payload = effect.effect_id;
    command.sort_key = layer + packed_y * 0x2000u + packed_x;
    // Original FUN_004f28c0 queues every visible active effect before any
    // definition/frame validation.  Special renderers (trail RNG, camera
    // shake, etc.) therefore still run when the generic sprite is absent.
    command.screen_y = effect.y + 1 - state.viewport_top;
    command.screen_x = effect.x - state.viewport_left;
    command.packed_flags = effect.flags;
    command.effect_runtime_context = &state;
    command.effect_runtime = &effect;
    return QueueGameplayRenderCommand(*state.render_queue, command);
}

bool owner_mask_includes_local_player(const UnitEffectRuntimeState& state, u32 owner) {
    if (state.players == nullptr ||
        owner >= state.players->owner_visibility_masks.size() ||
        state.local_player_slot >= 32) {
        return false;
    }
    return (state.players->owner_visibility_masks[owner] &
        (1u << state.local_player_slot)) != 0;
}

GameplayVisibilityUnit visibility_unit_from_effect_unit(
    const UnitEffectRuntimeState& state, const UnitMovementUnit& unit) {
    GameplayVisibilityUnit visibility{};
    visibility.owner_id = unit.owner_id;
    visibility.type_id = unit.type_id;
    visibility.variant = unit.action_mode_gate;
    visibility.runtime_flags = unit.runtime_flags;
    visibility.command_state = unit.command_state;
    visibility.command_flags = unit.command_flags;
    visibility.owner_visibility_mask =
        state.players != nullptr &&
            unit.owner_id < state.players->owner_visibility_masks.size()
            ? state.players->owner_visibility_masks[unit.owner_id]
            : 0;
    visibility.x = unit.x;
    visibility.y = unit.y;
    visibility.animation_timer = unit.animation_timer;
    visibility.movement_class = unit.definition.movement_class;
    visibility.center_x = unit.x + unit.definition.center_bounds_left +
        (unit.definition.center_bounds_width >> 1);
    visibility.center_y = unit.y + unit.definition.center_bounds_top +
        (unit.definition.center_bounds_height >> 1);
    visibility.visibility_probe_x = unit.x;
    visibility.visibility_probe_y = unit.y;
    visibility.owner_layer_probe_x = unit.current_cell_x;
    visibility.owner_layer_probe_y = unit.current_cell_y;
    visibility.terrain_probe_x = unit.current_cell_x;
    visibility.terrain_probe_y = unit.current_cell_y;
    visibility.terrain_probe_valid = true;
    return visibility;
}

bool unit_visible_for_projectile_effect(
    const UnitEffectRuntimeState& state, const UnitMovementUnit& unit) {
    const GameplayVisibilityUnit visibility =
        visibility_unit_from_effect_unit(state, unit);
    if (!CheckUnitVisibilityGateFlags(visibility)) {
        return true;
    }
    if (state.players == nullptr || state.visibility_grid == nullptr) {
        return true;
    }
    return CheckUnitOwnerMaskOrCurrentVisibilityBit(
        *state.players, *state.visibility_grid, visibility,
        state.local_player_slot);
}

bool world_point_visible_for_local_player(
    const UnitEffectRuntimeState& state, i32 world_x, i32 world_y) {
    if (state.visibility_grid == nullptr || state.visibility_grid->width == 0 ||
        state.visibility_grid->height == 0 ||
        state.visibility_grid->current.empty() ||
        state.local_player_slot + kGameplayVisibilityCurrentOwnerShift >= 32) {
        return true;
    }
    if (world_x < 0 || world_y < 0) {
        return false;
    }
    const u32 tile_x = static_cast<u32>(world_x) >> 5;
    const u32 tile_y = static_cast<u32>(world_y) >> 5;
    if (tile_x >= state.visibility_grid->width ||
        tile_y >= state.visibility_grid->height) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(tile_y) * state.visibility_grid->width + tile_x;
    if (index >= state.visibility_grid->current.size()) {
        return false;
    }
    const u32 bit = 1u << (state.local_player_slot +
        kGameplayVisibilityCurrentOwnerShift);
    return (state.visibility_grid->current[index] & bit) != 0;
}

bool projectile_camera_shake_in_view(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    const i32 width = std::max<i32>(0, state.viewport_right - state.viewport_left);
    const i32 height = std::max<i32>(0, state.viewport_bottom - state.viewport_top);
    const i32 half_width = width >> 1;
    const i32 half_height = height >> 1;
    const i32 center_x = state.viewport_left + half_width;
    const i32 center_y = state.viewport_top + half_height;
    return std::abs(center_x - effect.x) <= half_width + 0x96 &&
        std::abs(center_y - effect.y) <= half_height + 0x96;
}

void request_projectile_camera_shake(
    UnitEffectRuntimeState& state, const UnitEffectRuntime& effect) {
    if (state.callbacks.on_camera_shake != nullptr &&
        projectile_camera_shake_in_view(state, effect)) {
        state.callbacks.on_camera_shake(state, effect);
    }
}

u32 projectile_impact_entry_for_raw_image_index(
    const UnitEffectDefinition& definition, u32 raw_image_index) {
    if (raw_image_index < definition.image_resource_entries.size()) {
        return definition.image_resource_entries[raw_image_index];
    }
    return 0;
}

bool draw_projectile_parity_impact_sprite(
    const UnitEffectDefinition& definition, const UnitEffectRuntime& effect,
    i32 screen_x, i32 screen_y) {
    const u32 frame = effect_animation_frame(effect);
    if (frame >= definition.impact_image_indices.size()) {
        return true;
    }

    const u32 parity_class =
        static_cast<u32>(screen_x & 1) + static_cast<u32>(screen_y & 1);
    const u32 raw_image_index = definition.impact_image_indices[frame] +
        definition.impact_class_stride_factor *
            definition.impact_class_frame_count * parity_class;
    const u32 sprite_entry =
        projectile_impact_entry_for_raw_image_index(definition, raw_image_index);
    if (sprite_entry == 0) {
        return true;
    }
    DrawResourceSpriteMode(sprite_entry, screen_x, screen_y,
        definition.impact_draw_mode);
    return true;
}

bool draw_projectile_unit_group_impact_sprite(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 unit_type, i32 screen_x, i32 screen_y) {
    const UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
    if (target == nullptr) {
        return true;
    }

    const u32 sprite_entry = GetUnitDefinitionAnimationRowFrameResourceEntry(
        unit_type, kProjectileUnitImpactImageGroup, effect_animation_frame(effect),
        kProjectileUnitImpactImageGroup, kProjectileUnitImpactImageGroup,
        kProjectileUnitImpactRowIndex);
    if (sprite_entry == kInvalidResourceEntry) {
        return true;
    }

    SetSpriteUnitPaletteRamp(static_cast<u8>(target->owner_id));
    DrawResourceSpriteUnitRampToken1Shadow(sprite_entry, screen_x, screen_y);
    return true;
}

bool draw_projectile_direct_active_sprite(
    const UnitEffectDefinition& definition, const UnitEffectRuntime& effect,
    i32 screen_x, i32 screen_y) {
    const u32 frame = effect_animation_frame(effect);
    if (frame >= definition.active_sprite_entries.size()) {
        return true;
    }
    const u32 sprite_entry = definition.active_sprite_entries[frame];
    if (sprite_entry == 0) {
        return true;
    }
    if ((effect.flags & kUnitEffectFlagRefundOnFinish) != 0) {
        // Original 0x004f1ac8 -> 0x004d354a stores the caller's 32-bit
        // blend factor verbatim; values above 0x1f deliberately wrap in the
        // complementary-weight calculation instead of being clamped here.
        DrawResourceSpriteDirectBlendFactor(sprite_entry, screen_x, screen_y,
            effect.amount);
    } else {
        DrawResourceSpriteMode(sprite_entry, screen_x, screen_y,
            definition.active_draw_mode);
    }
    return true;
}

void append_trail_segment(UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1,
    u32 width, u16 color) {
    UnitEffectTrailSegment segment{};
    segment.effect_id = effect.effect_id;
    segment.x0 = x0;
    segment.y0 = y0;
    segment.x1 = x1;
    segment.y1 = y1;
    segment.width = width;
    segment.color = color;
    state.trail_segments.push_back(segment);
}

bool queue_unit_effect_command(UnitMovementUnit& source, UnitMovementUnit* target,
    i32 x, i32 y, u32 command_state) {
    return SetOrQueueUnitExtendedStateCommand(
        &source, target, x, y, command_state, false);
}

template <typename Predicate>
UnitMovementUnit* find_first_effect_unit_in_range(UnitEffectRuntimeState& state,
    const UnitMovementUnit& source, u32 range, Predicate predicate,
    bool include_source = false) {
    if (!state.unit_refs.empty()) {
        for (UnitMovementUnit* unit : state.unit_refs) {
            if (unit == nullptr || !unit->active ||
                (!include_source && unit->id == source.id) ||
                !predicate(*unit)) {
                continue;
            }
            const u32 distance =
                CalculateApproxUnitDistance(source.x, source.y, unit->x, unit->y);
            if (distance <= range) {
                return unit;
            }
        }
        return nullptr;
    }

    for (UnitMovementUnit& unit : state.units) {
        if (!unit.active || (!include_source && unit.id == source.id) ||
            !predicate(unit)) {
            continue;
        }
        const u32 distance =
            CalculateApproxUnitDistance(source.x, source.y, unit.x, unit.y);
        if (distance <= range) {
            return &unit;
        }
    }
    return nullptr;
}

template <typename Callback>
void for_each_effect_unit_in_active_order(UnitEffectRuntimeState& state,
    Callback callback) {
    if (!state.unit_refs.empty()) {
        for (UnitMovementUnit* unit : state.unit_refs) {
            if (unit != nullptr) {
                callback(*unit);
            }
        }
        return;
    }
    for (UnitMovementUnit& unit : state.units) {
        callback(unit);
    }
}

bool unit_owner_bit_set(u32 owner_mask, const UnitMovementUnit& unit) {
    if (unit.owner_id >= 32) {
        return false;
    }
    return (owner_mask & (1u << unit.owner_id)) != 0;
}

bool unit_effect_related_owner_rejected(const UnitEffectRuntimeState& state,
    const UnitMovementUnit* source, const UnitMovementUnit& candidate,
    const UnitEffectDefinition& definition, u32 direct_target_id = 0) {
    if (definition.area_damage_allows_related_targets || source == nullptr) {
        return false;
    }
    if (candidate.id == direct_target_id) {
        return false;
    }
    if (source->owner_id == candidate.owner_id) {
        return true;
    }
    if (state.players == nullptr ||
        source->owner_id >= state.players->owner_relation_masks.size() ||
        candidate.owner_id >= 32) {
        return false;
    }
    return (state.players->owner_relation_masks[source->owner_id] &
        (1u << candidate.owner_id)) != 0;
}

bool point_inside_unit_bounds(const UnitMovementUnit& unit, i32 x, i32 y) {
    const i32 left = action_center_x(unit) -
        std::max<i32>(unit.definition.bounds_width, 1) / 2;
    const i32 top = action_center_y(unit) -
        std::max<i32>(unit.definition.bounds_height, 1) / 2;
    const i32 right = left + std::max<i32>(unit.definition.bounds_width, 1);
    const i32 bottom = top + std::max<i32>(unit.definition.bounds_height, 1);
    return x >= left && x <= right && y >= top && y <= bottom;
}

bool effect_unit_inside_impact_area(const UnitEffectRuntime& effect,
    const UnitMovementUnit& unit, u32 radius) {
    if (radius == 0 || unit.definition.render_class == 1) {
        return point_inside_unit_bounds(unit, effect.x, effect.y);
    }
    return CalculateApproxUnitDistance(effect.x, effect.y, unit.x, unit.y) < radius;
}

u32 effect_unit_area_damage_amount(const UnitEffectRuntime& effect,
    const UnitMovementUnit& unit, u32 amount, u32 radius) {
    if (radius == 0 || unit.definition.render_class == 1) {
        return amount;
    }
    const u32 distance = CalculateApproxUnitDistance(effect.x, effect.y,
        unit.x, unit.y);
    if (distance >= radius) {
        return 0;
    }
    return amount - static_cast<u32>(
        (static_cast<u64>(distance) * amount) / radius);
}

bool unit_effect_area_stun_candidate_allowed(const UnitEffectRuntime& effect,
    const UnitMovementUnit& unit, u32 radius) {
    if (!unit.active || unit.type_id >= 0x60 ||
        (unit.runtime_flags & kUnitEffectAreaStunRuntimeSkipMask) != 0 ||
        unit.definition.render_class == 3) {
        return false;
    }
    return CalculateApproxUnitDistance(effect.x, effect.y, unit.x, unit.y) <= radius;
}

void apply_unit_effect_area_stun_side_effect(UnitMovementUnit& unit) {
    unit.target = nullptr;
    unit.command_state = kUnitStateRandomRelocation;
    unit.command_value = 0;
    unit.distance_check_mode = 0;
    unit.work_timer = 0;
    unit.command_lockout_ticks = 0;
    unit.command_entry_lockout_ticks = 0;
    unit.animation_timer = 0;
    // Original FUN_004ef3cb clears raw unit +0x110 here.  That field is the
    // movement step accumulator (also temporarily reused by obstacle probes),
    // not the raw +0xb4 direction-turn timeout counter.
    unit.movement_step_accumulator = 0;
    unit.movement_residual_x = 0;
    unit.movement_residual_y = 0;
    unit.movement_interpolation_x = 0.0f;
    unit.movement_interpolation_y = 0.0f;
    unit.runtime_flags |= 0x08000000u;
    unit.runtime_flags &= 0xfffdff9fu;
    unit.command_flags &= ~0x1000u;
}

bool chain_target_id_excluded(const UnitEffectRuntime& effect, u32 unit_id) {
    if (unit_id == effect.target_unit_id) {
        return true;
    }
    return std::find(effect.chained_target_ids.begin(),
        effect.chained_target_ids.end(), unit_id) != effect.chained_target_ids.end();
}

u32 source_owner_relation_mask(const UnitEffectRuntimeState& state,
    const UnitMovementUnit& source) {
    if (state.players != nullptr &&
        source.owner_id < state.players->owner_relation_masks.size()) {
        return state.players->owner_relation_masks[source.owner_id];
    }
    return source.owner_id < 32 ? (1u << source.owner_id) : 0;
}

u32 chain_source_relation_mask(const UnitEffectRuntimeState& state,
    const UnitMovementUnit& source) {
    return source_owner_relation_mask(state, source);
}

bool chain_candidate_rejected_by_runtime_gates(const UnitEffectRuntimeState& state,
    const UnitMovementUnit& source, const UnitMovementUnit& candidate) {
    constexpr u32 kOriginalChainTargetRuntimeSkipMask = 0x20000006u;
    if ((candidate.runtime_flags & kOriginalChainTargetRuntimeSkipMask) != 0 ||
        (candidate.command_state & kUnitCommandDead) != 0) {
        return true;
    }
    if (candidate.owner_id < 32 &&
        (chain_source_relation_mask(state, source) & (1u << candidate.owner_id)) != 0) {
        return true;
    }
    return false;
}

bool unit_effect_candidate_allowed_by_render_class_mask(
    const UnitEffectDefinition* definition, const UnitMovementUnit& candidate) {
    if (definition == nullptr || candidate.definition.render_class >= 32) {
        return true;
    }
    return (definition->allowed_target_render_class_mask &
        (1u << candidate.definition.render_class)) != 0;
}

bool unit_effect_area_candidate_passes_common_gates(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect,
    const UnitMovementUnit* source, const UnitMovementUnit& candidate,
    const UnitEffectDefinition& definition,
    u32 runtime_skip_mask = kUnitEffectPointImpactRuntimeSkipMask,
    bool apply_definition_render_class_mask = true) {
    if (!candidate.active ||
        (candidate.runtime_flags & runtime_skip_mask) != 0 ||
        (candidate.command_state & kUnitCommandDead) != 0) {
        return false;
    }
    if (candidate.id == effect.source_unit_id &&
        candidate.id != effect.target_unit_id) {
        return false;
    }
    if (apply_definition_render_class_mask &&
        !unit_effect_candidate_allowed_by_render_class_mask(&definition, candidate)) {
        return false;
    }
    return !unit_effect_related_owner_rejected(
        state, source, candidate, definition, effect.target_unit_id);
}

UnitEffectDefinition fallback_unit_effect_area_definition() {
    UnitEffectDefinition definition{};
    definition.allowed_target_render_class_mask = 0xffffffffu;
    return definition;
}

const UnitEffectDefinition& unit_effect_area_definition_or_fallback(
    const UnitEffectRuntimeState& state, const UnitEffectRuntime& effect,
    UnitEffectDefinition& fallback) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition != nullptr) {
        return *definition;
    }
    fallback = fallback_unit_effect_area_definition();
    return fallback;
}

bool unit_effect_area_source_allows_scan(const UnitMovementUnit* source) {
    if (source == nullptr) {
        return true;
    }
    return source->command_state == 0x45 || (source->runtime_flags & 0x80u) == 0;
}

bool apply_unit_effect_raw_health_damage(UnitEffectRuntimeState& state,
    UnitMovementUnit& target, u32 amount) {
    if (amount == 0 || !target.active ||
        (target.command_state & kUnitCommandDead) != 0 ||
        (target.runtime_flags & kUnitRuntimeHiddenOrInactive) != 0) {
        return false;
    }

    // FUN_004c212c routes this direct mutation through the target's shield
    // record before touching HP.  This is intentionally not an impact event:
    // action 0x18 calls the helper directly and the shared event drain would
    // apply the same point of damage a second time.
    if ((target.runtime_flags & 0x100u) != 0) {
        if (UnitEffectRuntime* shield =
                effect_slot_from_original_offset_allow_inactive(
                    state, target.linked_effect_slot_offset)) {
            // FUN_004c212c follows raw unit +0xf0 directly.  It neither tests
            // list ownership nor revalidates the stale node's id/source.
            if (shield->amount > amount) {
                shield->amount -= amount;
                return false;
            }
            amount -= shield->amount;
            target.runtime_flags &= ~0x100u;
            ReleaseUnitEffectSlot(state, *shield);
        }
    }

    // Raw +0x30 is overloaded: it is equipment slot zero for mobile types,
    // but the construction/action-mode gate for class-0x60+ objects.  The
    // original has already gated this branch on type >= 0x60, so marker one
    // means an unfinished structure rather than an item or status timer.
    if (target.type_id >= 0x60u && target.action_mode_gate == 1) {
        amount += amount;
    }
    if (target.health <= amount) {
        target.health = 0;
        target.command_state |= kUnitCommandDead;
        return true;
    }
    target.health -= amount;
    return false;
}

u32 unit_effect_point_impact_damage(UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect, UnitMovementUnit* source,
    UnitMovementUnit& target) {
    if (state.callbacks.calculate_impact_damage != nullptr) {
        return state.callbacks.calculate_impact_damage(
            state, effect, source, target);
    }
    return effect.amount;
}

u32 chain_effect_candidate_range(const UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect, const UnitMovementUnit* source,
    const UnitMovementUnit& current_target) {
    if (source != nullptr) {
        const u32 action_range = CalculateUnitActionRangeWithProductionAndEquipmentEffects(
            production_state_or_empty(state), *source,
            current_target.definition.render_class, state.equipment_catalog);
        if (action_range != 0) {
            return action_range >> 1;
        }
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    u32 range = effect.range != 0 ? effect.range :
        (definition != nullptr ? definition->impact_radius : 0);
    return range != 0 ? range : 0x80;
}

bool chain_candidate_passes_range_gate(const UnitMovementUnit& current_target,
    const UnitMovementUnit& candidate, u32 range) {
    if (candidate.definition.render_class == 1) {
        return point_inside_unit_bounds(candidate, current_target.x, current_target.y);
    }
    if (range == 0) {
        return false;
    }
    const u32 distance = CalculateApproxUnitDistance(
        current_target.x, current_target.y,
        action_center_x(candidate), action_center_y(candidate));
    return distance < range;
}

bool chain_candidate_visible_to_source_owner(const UnitEffectRuntimeState& state,
    const UnitMovementUnit& source, const UnitMovementUnit& candidate) {
    if (state.players == nullptr || state.visibility_grid == nullptr ||
        source.owner_id >= 32) {
        return true;
    }
    const GameplayVisibilityUnit visibility =
        visibility_unit_from_effect_unit(state, candidate);
    if (!CheckUnitVisibilityGateFlags(visibility)) {
        return true;
    }
    return CheckUnitOwnerMaskOrCurrentVisibilityBit(*state.players,
        *state.visibility_grid, visibility, source.owner_id);
}

bool effect_uses_zero_reach_frame(u32 effect_id) {
    return effect_id == 0x14 || effect_id == 0x17 ||
        effect_id == 0x27 || effect_id == 0x28 || effect_id == 0x2c;
}

void append_chain_target_id(UnitEffectRuntime& effect, u32 unit_id) {
    for (u32& chained_id : effect.chained_target_ids) {
        if (chained_id == 0) {
            chained_id = unit_id;
            return;
        }
    }
    effect.chained_target_ids.back() = unit_id;
}

u32 chain_effect_damage_amount(const UnitEffectRuntime& effect, u32 amount) {
    if (effect.effect_id != 0x17 || effect.chained_target_ids[0] == 0) {
        return amount;
    }

    u32 divisor = effect.chain_remaining + 2;
    if (effect.chained_target_ids[1] != 0) {
        ++divisor;
    }
    if (effect.chained_target_ids[2] != 0) {
        ++divisor;
    }
    if (divisor == 0) {
        return amount;
    }

    amount /= divisor;
    amount *= effect.chain_remaining + 1;
    return amount;
}

void apply_action_effect_target_lockout_if_flagged(UnitMovementUnit& target,
    u32 lockout_ticks) {
    if ((target.definition.action_effect_flags & 0x8u) == 0) {
        return;
    }
    target.runtime_flags |= 0x40u;
    target.command_lockout_ticks =
        std::max(target.command_lockout_ticks, lockout_ticks);
}

UnitMovementUnit* find_chain_effect_next_target(UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect) {
    UnitMovementUnit* current_target = find_effect_unit(state, effect.target_unit_id);
    UnitMovementUnit* original_source = find_effect_unit(state, effect.source_unit_id);
    if (current_target == nullptr) {
        return nullptr;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const u32 range =
        chain_effect_candidate_range(state, effect, original_source, *current_target);

    const auto accepted = [&](UnitMovementUnit& unit) {
        if (!unit.active || unit.id == effect.source_unit_id ||
            chain_target_id_excluded(effect, unit.id) ||
            (unit.runtime_flags & kUnitActionTargetTransient) != 0 ||
            !unit_effect_candidate_allowed_by_render_class_mask(definition, unit) ||
            (original_source != nullptr &&
                chain_candidate_rejected_by_runtime_gates(
                    state, *original_source, unit)) ||
            (original_source != nullptr &&
                !chain_candidate_visible_to_source_owner(
                    state, *original_source, unit))) {
            return false;
        }
        return chain_candidate_passes_range_gate(*current_target, unit, range);
    };

    if (!state.unit_refs.empty()) {
        for (UnitMovementUnit* unit : state.unit_refs) {
            if (unit != nullptr && accepted(*unit)) {
                return unit;
            }
        }
    } else {
        for (UnitMovementUnit& unit : state.units) {
            if (accepted(unit)) {
                return &unit;
            }
        }
    }
    return nullptr;
}

enum class UnitEffectActionStartKind {
    direct_damage_only,
    default_path,
    target_center_impact,
    source_position_impact,
    status_chain_path,
    source_muzzle_to_target_impact,
};

UnitEffectActionStartKind action_effect_start_kind(u32 effect_id) {
    switch (effect_id) {
    case 0x00:
    case 0x01:
    case 0x08:
    case 0x0c:
    case 0x0d:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x1a:
    case 0x1b:
    case 0x1d:
    case 0x23:
    case 0x24:
        return UnitEffectActionStartKind::direct_damage_only;
    case 0x17:
        return UnitEffectActionStartKind::status_chain_path;
    case 0x1e:
        return UnitEffectActionStartKind::source_muzzle_to_target_impact;
    case 0x22:
        return UnitEffectActionStartKind::target_center_impact;
    case 0x26:
        return UnitEffectActionStartKind::source_position_impact;
    default:
        return UnitEffectActionStartKind::default_path;
    }
}

void prepare_non_slot_action_effect(UnitEffectRuntime& effect, u32 effect_id,
    UnitMovementUnit& source, UnitMovementUnit* target) {
    effect = {};
    effect.effect_id = effect_id;
    effect.source_unit_id = source.id;
    effect.target_unit_id = target != nullptr ? target->id : 0;
    effect.linked_unit_id = effect.target_unit_id;
    effect.x = source.x;
    effect.y = source.y;
    effect.target_x = target != nullptr ? target->x : source.x;
    effect.target_y = target != nullptr ? target->y : source.y;
}

u32 chain_count_from_source_status(const UnitMovementUnit& source) {
    if (source.status_timer < 2) {
        return 1;
    }
    return source.status_timer < 4 ? 2 : 3;
}

} // namespace

bool CheckIncomingActionTargetTransientFlag(const UnitMovementUnit& target) {
    return (target.runtime_flags & kUnitActionTargetTransient) != 0;
}

bool CheckStoredActionTargetTransientFlag(const UnitMovementUnit& source) {
    return source.target != nullptr &&
        CheckIncomingActionTargetTransientFlag(*source.target);
}

bool CheckUnitActionImpactFrame(const UnitMovementUnit& source) {
    const u32 count = std::min<u32>(source.definition.action_impact_frame_count,
        static_cast<u32>(source.definition.action_impact_frames.size()));
    for (u32 index = 0; index < count; ++index) {
        if (source.definition.action_impact_frames[index] == source.animation_frame) {
            return true;
        }
    }
    return false;
}

bool CheckUnitActionTargetClassGate(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    if ((target.runtime_flags & kUnitActionTargetClassBlocked) != 0) {
        return false;
    }
    if (context.callbacks.can_target != nullptr) {
        return context.callbacks.can_target(context, source, target);
    }
    return target.active && (target.command_state & kUnitCommandDead) == 0;
}

UnitActionTargetValidation ValidateUnitActionTarget(UnitActionContext& context,
    const UnitMovementUnit& source, const UnitMovementUnit& target) {
    UnitActionTargetValidation result;
    if (&source == &target ||
        (target.runtime_flags & (kUnitActionTargetTransient |
            kUnitActionTargetInactive)) != 0 ||
        source.distance_check_mode == 1) {
        return result;
    }
    if (!CheckUnitActionTargetClassGate(context, source, target)) {
        return result;
    }

    if (context.callbacks.validate_target_reach != nullptr) {
        return context.callbacks.validate_target_reach(context, source, target);
    }
    return default_target_reach_validation(context, source, target);
}

void BeginUnitActionAnimation(UnitActionContext& context, UnitMovementUnit& source) {
    if (context.callbacks.on_begin_action_animation != nullptr) {
        context.callbacks.on_begin_action_animation(context, source);
    }
}

void ApplyUnitActionImpact(UnitActionContext& context, UnitMovementUnit& source,
    UnitMovementUnit& target) {
    if (context.callbacks.on_action_impact != nullptr) {
        context.callbacks.on_action_impact(context, source, target);
    }
}

UnitActionTickResult ProcessUnitActionCycle(UnitActionContext& context,
    UnitMovementUnit& source) {
    UnitActionTickResult result;
    UnitMovementUnit* target = source.target;
    result.target = target;
    if (target == nullptr ||
        (target->runtime_flags & kUnitActionTargetInactive) != 0) {
        source.command_flags &= ~kUnitActionCommandStarted;
        if (context.callbacks.on_target_lost != nullptr) {
            context.callbacks.on_target_lost(context, source);
        }
        return result;
    }

    if ((source.command_flags & kUnitActionCommandStarted) == 0) {
        // FUN_004c211c rejects the raw target bit-4 state before recovery,
        // but the class/owner/range validation in FUN_004c1e85 happens only
        // after the raw +0xf4 recovery branch.
        if (CheckStoredActionTargetTransientFlag(source)) {
            source.command_flags &= ~kUnitActionCommandStarted;
            return result;
        }
        if (source.command_lockout_ticks != 0) {
            result.valid_target = true;
            update_action_direction_to_target_center(context, source, *target);
            result.code = UnitActionTickCode::turning_to_target;
            return result;
        }
        const UnitActionTargetValidation validation =
            ValidateUnitActionTarget(context, source, *target);
        result.valid_target = validation.valid;
        result.distance = validation.distance;
        if (!validation.valid) {
            source.command_flags &= ~kUnitActionCommandStarted;
            return result;
        }
        if (!validation.in_range) {
            source.path_target_x = target->x;
            source.path_target_y = target->y;
            return result;
        }

        if (context.callbacks.try_auto_effect_command != nullptr &&
            context.callbacks.try_auto_effect_command(context, source,
                validation.command_x, validation.command_y)) {
            result.code = UnitActionTickCode::cycle_started;
            return result;
        }

        BeginUnitActionAnimation(context, source);
        source.command_flags |= kUnitActionCommandStarted;
        source.command_flags &= ~kUnitActionCommandClearMask;
        source.animation_frame = 0;
        result.code = UnitActionTickCode::cycle_started;
        return result;
    }

    bool skip_active_frame_body = false;
    if ((source.command_flags & kUnitActionImpactApplied) == 0 &&
        CheckStoredActionTargetTransientFlag(source)) {
        // At 0x004c1d22 the original skips straight to the frame advance when
        // the target profile's +0x240 replacement gate is zero.  Facing,
        // sound, and impact are intentionally omitted for that frame.
        if (!can_replace_transient_target(context, source, *target)) {
            skip_active_frame_body = true;
        }
        else {
            UnitMovementUnit* replacement =
                context.callbacks.find_replacement_target != nullptr
                    ? context.callbacks.find_replacement_target(context, source)
                    : nullptr;
            if (replacement == nullptr) {
                source.command_flags &= ~kUnitActionCommandStarted;
                if (context.callbacks.on_target_lost != nullptr) {
                    context.callbacks.on_target_lost(context, source);
                }
                return result;
            }

            source.target = replacement;
            source.path_target_x = replacement->x;
            source.path_target_y = replacement->y;
            target = replacement;
            result.target = target;

            const UnitActionTargetValidation validation =
                ValidateUnitActionTarget(context, source, *target);
            result.valid_target = validation.valid;
            result.distance = validation.distance;
            if (!validation.valid || !validation.in_range) {
                source.command_flags &= ~kUnitActionCommandStarted;
                return result;
            }
        }
    }

    if (!skip_active_frame_body) {
        update_action_direction_to_target_center(context, source, *target);
        if (context.callbacks.on_action_sound != nullptr) {
            context.callbacks.on_action_sound(context, source);
        }

        result.impact_frame = CheckUnitActionImpactFrame(source);
        if (result.impact_frame) {
            ApplyUnitActionImpact(context, source, *target);
            source.command_flags |= kUnitActionImpactApplied;
        }
    }

    ++source.animation_frame;
    if (source.animation_frame >= action_cycle_ticks(source)) {
        source.command_flags &= ~(kUnitActionCommandStarted |
            kUnitActionImpactApplied);
        source.animation_frame = 0;
        const u32 recovery_ticks =
            CalculateUnitActionRecoveryTicksWithProductionAndEquipmentEffects(
                production_state_or_empty(context), source, context.equipment_catalog);
        if (recovery_ticks != 0) {
            source.command_lockout_ticks = recovery_ticks;
        }
        if (context.callbacks.on_action_cycle_complete != nullptr) {
            context.callbacks.on_action_cycle_complete(context, source);
        }
        result.code = UnitActionTickCode::cycle_complete;
        return result;
    }

    result.code = UnitActionTickCode::cycle_in_progress;
    return result;
}

void TickUnitEffectRuntime(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (!effect.active) {
        return;
    }
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupDelay(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        DispatchUnitEffectImpactState(state, effect);
        return;
    }
    DispatchUnitEffectActiveState(state, effect);
}

void TickUnitEffectStartupDelay(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    ++effect.tick;
    if (definition != nullptr && effect.tick < definition->startup_ticks) {
        return;
    }
    finish_effect(state, effect);
}

void DispatchUnitEffectActiveState(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (effect.effect_id < 0x3d &&
        action_effect_start_kind(effect.effect_id) ==
            UnitEffectActionStartKind::direct_damage_only) {
        finish_effect(state, effect);
        return;
    }
    if (effect.effect_id < 0x3d) {
        TickUnitEffectPathActive(state, effect);
        return;
    }
    TickUnitEffectFrameAndApplyImpacts(state, effect);
}

void DispatchUnitEffectImpactState(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (effect.effect_id < 0x3d) {
        if (effect.effect_id == 0x17) {
            TickUnitEffectChainImpact(state, effect);
            return;
        }
        if (effect.effect_id == 0x1e) {
            TickUnitEffectSourceMuzzleLineImpact(state, effect);
            return;
        }
        if (effect.effect_id == 0x22 || effect.effect_id == 0x26) {
            TickUnitEffectInitialDamageImpact(state, effect);
            return;
        }
        if (effect.effect_id == 0x27) {
            TickUnitEffectTargetMarkerImpact(state, effect);
            return;
        }
        if (action_effect_start_kind(effect.effect_id) ==
            UnitEffectActionStartKind::direct_damage_only) {
            finish_effect(state, effect);
            return;
        }
        TickUnitEffectFrameAndApplyImpacts(state, effect);
        return;
    }
    TickUnitEffectLinkedTargetFrames(state, effect);
}

void TickUnitEffectChainImpact(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (effect.chain_remaining == 0) {
        TickUnitEffectFrameAndApplyImpacts(state, effect);
        return;
    }

    UnitMovementUnit* next_target = find_chain_effect_next_target(state, effect);
    UnitMovementUnit* current_target = find_effect_unit(state, effect.target_unit_id);
    UnitMovementUnit* original_source = find_effect_unit(state, effect.source_unit_id);
    if (next_target == nullptr || current_target == nullptr || original_source == nullptr) {
        effect.chain_remaining = 0;
        TickUnitEffectFrameAndApplyImpacts(state, effect);
        return;
    }

    UnitEffectRuntime* child = AllocateUnitEffectSlot(state);
    if (child == nullptr) {
        effect.chain_remaining = 0;
        TickUnitEffectFrameAndApplyImpacts(state, effect);
        return;
    }

    if (!BeginUnitEffectImmediate(state, *child, effect.effect_id, *current_target,
            next_target)) {
        ReleaseUnitEffectSlot(state, *child);
        effect.chain_remaining = 0;
        TickUnitEffectFrameAndApplyImpacts(state, effect);
        return;
    }
    // The low-id default path initializer clears raw +0x4c before recording
    // any chain hits at +0x50.  This vector is the reconstruction-only mirror
    // of that count/list pair and must not survive pool reuse.
    child->hit_unit_ids.clear();
    QueueUnitEffectStartSoundIfAny(state, *child);
    InitializeUnitEffectPathToTarget(state, *child, *current_target, *next_target);
    child->source_unit_id = effect.source_unit_id;
    child->linked_unit_id = next_target->id;
    child->chain_remaining = effect.chain_remaining - 1;
    child->chained_target_ids = effect.chained_target_ids;
    append_chain_target_id(*child, current_target->id);
    child->x = effect.x;
    child->y = effect.y;
    // Original 0x004ecadc copies raw +0x0c to the chained child after its
    // path has been initialized. In the typed runtime that word is `tick`.
    child->tick = effect.tick;

    effect.chain_remaining = 0;
    TickUnitEffectFrameAndApplyImpacts(state, effect);
}

void TickUnitEffectSourceMuzzleLineImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
    if (source == nullptr || (source->runtime_flags & kUnitActionTargetTransient) != 0) {
        finish_effect(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (effect.tick == 0 && target != nullptr) {
        const u32 amount = unit_effect_point_impact_damage(
            state, effect, source, *target);
        append_effect_event(state, UnitEffectEventKind::impact, effect,
            target->id, amount);
    }

    ++effect.tick;
    ++effect.frame;
    if (effect.tick >= impact_render_ticks(definition)) {
        finish_effect(state, effect);
        return;
    }

    if (target != nullptr) {
        CalculateUnitEffectSourceAndTargetCenters(state, effect, *source, target);
        effect.previous_x = effect.target_x;
        effect.previous_y = effect.target_y;
    }
}

void TickUnitEffectInitialDamageImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (source == nullptr || (source->runtime_flags & kUnitActionTargetTransient) != 0) {
        finish_effect(state, effect);
        return;
    }

    if (!effect.initial_impact_applied) {
        effect.initial_impact_applied = true;
        if (UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id)) {
            const u32 amount = unit_effect_point_impact_damage(
                state, effect, source, *target);
            append_effect_event(state, UnitEffectEventKind::impact, effect,
                target->id, amount);
        }
    }
    TickUnitEffectFrameAndApplyImpacts(state, effect);
}

void TickUnitEffectTargetMarkerImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
    if (target == nullptr || (target->runtime_flags & kUnitActionTargetTransient) != 0) {
        finish_effect(state, effect);
        return;
    }

    effect.x = target->x;
    effect.y = target->y;
    ++effect.frame;
    if ((target->runtime_flags & 0x20000u) != 0) {
        if (effect.frame >= 9) {
            effect.frame = 0;
        }
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (effect.frame >= active_frame_count(definition)) {
        finish_effect(state, effect);
    }
}

void TickUnitEffectPathActive(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        finish_effect(state, effect);
        return;
    }

    if (effect.effect_id == 0x20 &&
        (effect.flags & kUnitEffectFlagAfterimageClone) != 0) {
        ++effect.tick;
        if (effect.tick >= definition->render_ticks * 2) {
            finish_effect(state, effect);
            return;
        }
        effect.frame = effect.tick >> 1;
        return;
    }

    const bool chain_path_active = effect.effect_id == 0x17;
    // The original generic low-id effect path (0x004ec785) consumes the
    // remaining path budget stored at effect + 0x10.  InitializeUnitEffectPathToTarget
    // mirrors that field in `range`; `tick` mirrors a separate render timer and
    // starts at zero.  Consuming `tick` here therefore discarded every ordinary
    // projectile before its first movement step.
    if (effect.range == 0 && !chain_path_active) {
        finish_effect(state, effect);
        return;
    }

    if (effect.effect_id == 0x20) {
        if (UnitEffectRuntime* child = AllocateUnitEffectSlot(state)) {
            // The 0x004ec69c afterimage initializer copies only the visual
            // position/direction words from its parent.  Every other payload
            // word remains whatever this recycled pool node already held.
            child->effect_id = effect.effect_id;
            child->flags = effect.flags | kUnitEffectFlagAfterimageClone;
            child->tick = 0;
            child->frame = 0;
            child->x = effect.x;
            child->y = effect.y;
            child->direction = effect.direction;
        }
    }

    // 0x004ec798/0x004ec7cd decrements the raw +0x194 iteration counter after
    // each unsuccessful step and loops while it is nonzero.  This is a
    // do-while counter: a catalog value of zero wraps to 0xffffffff instead of
    // skipping movement.  The path budget below still bounds that case and is
    // what makes effects such as 0x2a reach a nearby target in the same tick.
    u32 remaining_iterations = definition->active_step_iterations;
    do {
        if (!chain_path_active && effect.range != 0) {
            --effect.range;
            if (effect.range == 0) {
                finish_effect(state, effect);
                return;
            }
        }

        if ((definition->behavior_flags & kUnitEffectBehaviorBoundsImpact) != 0) {
            RetargetUnitEffectProjectilePath(state, effect);
        }
        if ((definition->behavior_flags & kUnitEffectBehaviorSkipImpactCheck) != 0) {
            ApplyUnitEffectPointImpactAndSpawnChildren(state, effect);
            if (!effect.active) {
                return;
            }
        }
        AdvanceUnitEffectProjectileTowardTarget(state, effect);
        if ((effect.flags & kUnitEffectFlagImpact) != 0) {
            // Original 0x004ec813 seeds raw +0x0c at one. That word is `tick`
            // in the typed runtime; raw +0x10/`frame` is not the impact
            // animation counter and has already been cleared by reach.
            effect.tick = effect_uses_zero_reach_frame(effect.effect_id) ? 0 : 1;
            UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
            if (target == nullptr ||
                (target->runtime_flags & kUnitActionTargetTransient) != 0) {
                finish_effect(state, effect);
                return;
            }
            UnitMovementUnit* source =
                find_effect_unit(state, effect.source_unit_id);
            const u32 base_amount = unit_effect_point_impact_damage(
                state, effect, source, *target);
            bool finish_after_reach = false;
            if (effect.effect_id == 0x14 || effect.effect_id == 0x28 ||
                effect.effect_id == 0x2c) {
                const u32 lockout_ticks =
                    std::min<u32>((base_amount >> 2) + 1, 0x0b);
                apply_action_effect_target_lockout_if_flagged(*target, lockout_ticks);
            }
            if (effect.effect_id == 0x27) {
                effect.x = target->x;
                effect.y = target->y;
                const bool marker_already_active =
                    (target->runtime_flags & 0x20000u) != 0;
                target->runtime_flags |= 0x20000u;
                const u32 lockout_ticks =
                    std::min<u32>(base_amount * 2 + 1, 0xffffu);
                target->command_lockout_ticks =
                    std::max(target->command_lockout_ticks, lockout_ticks);
                finish_after_reach = marker_already_active;
            }
            // 0x004ec3be uses the calculated action damage only to seed the
            // target marker duration for effect 0x27.  Every other low-id
            // reach handler applies that freshly calculated value; raw effect
            // +0x14 is deliberately left untouched by the action initializer.
            if (effect.effect_id != 0x27) {
                append_effect_event(state, UnitEffectEventKind::impact, effect,
                    target->id,
                    chain_effect_damage_amount(effect, base_amount));
            }
            if (finish_after_reach) {
                finish_effect(state, effect);
            }
            return;
        }
        --remaining_iterations;
    } while (remaining_iterations != 0);

    const u32 frames = active_frame_count(definition);
    effect.tick = frames == 0 ? 0 : (effect.tick + 1) % frames;
}

bool BeginUnitEffectStartup(UnitEffectRuntimeState& state, UnitEffectRuntime& effect,
    u32 effect_id, UnitMovementUnit& source, UnitMovementUnit* target) {
    static_cast<void>(target);
    const UnitEffectDefinition* definition = find_effect_definition(state, effect_id);
    if (definition == nullptr || definition->startup_ticks == 0) {
        return false;
    }

    // FUN_004ece98 writes only raw +0x00, +0x08, +0x0c, +0x10, +0x18,
    // +0x20 and +0x24.  Direction, amount, target and all path payload words
    // deliberately survive effect-pool reuse.
    effect.active = true;
    effect.effect_id = effect_id;
    effect.flags = kUnitEffectFlagStartup;
    effect.tick = 0;
    effect.frame = 0;
    effect.source_unit_id = source.id;
    if (definition->startup_uses_source_muzzle) {
        const UnitMovementPoint source_delta = unit_effect_source_offset(source);
        effect.x = source.x + source_delta.x;
        effect.y = source.y + source_delta.y;
    } else {
        effect.x = source.x;
        effect.y = source.y;
    }
    append_effect_event(state, UnitEffectEventKind::started, effect);
    return true;
}

void DispatchUnitEffectStartByAction(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 effect_id, UnitMovementUnit& source,
    UnitMovementUnit* target) {
    const UnitEffectActionStartKind start_kind = action_effect_start_kind(effect_id);
    if (start_kind == UnitEffectActionStartKind::direct_damage_only) {
        prepare_non_slot_action_effect(effect, effect_id, source, target);
        QueueUnitEffectStartSoundIfAny(state, effect);
        return;
    }

    const u32 retained_slot_direction = effect.direction;
    const i32 retained_slot_target_x = effect.target_x;
    const i32 retained_slot_target_y = effect.target_y;
    if (!BeginUnitEffectImmediate(state, effect, effect_id, source, target)) {
        return;
    }
    QueueUnitEffectStartSoundIfAny(state, effect);
    if (start_kind == UnitEffectActionStartKind::target_center_impact &&
        target != nullptr) {
        effect.x = action_center_x(*target);
        effect.y = action_center_y(*target);
        effect.direction = 0;
        effect.flags = kUnitEffectFlagImpact;
        // Reconstruction-only latch; unlike raw effect payload it starts
        // fresh for every one-shot 0x22 impact.
        effect.initial_impact_applied = false;
        return;
    }
    if (start_kind == UnitEffectActionStartKind::source_position_impact) {
        effect.x = source.x;
        effect.y = source.y;
        effect.direction = 0;
        effect.flags = kUnitEffectFlagImpact;
        effect.initial_impact_applied = false;
        return;
    }
    if (start_kind == UnitEffectActionStartKind::source_muzzle_to_target_impact) {
        CalculateUnitEffectSourceAndTargetCenters(state, effect, source, target);
        const i32 target_center_x = effect.target_x;
        const i32 target_center_y = effect.target_y;
        // The 0x004ed01c initializer never writes raw +0x04.  Its shared
        // center calculation is direction-free in the original and writes
        // the target center to raw +0x30/+0x34, leaving +0x28/+0x2c stale.
        effect.direction = retained_slot_direction;
        effect.target_x = retained_slot_target_x;
        effect.target_y = retained_slot_target_y;
        effect.previous_x = target_center_x;
        effect.previous_y = target_center_y;
        effect.flags = kUnitEffectFlagImpact;
        return;
    }
    // FUN_004ed189 initializes the low-id path scratch with +0x48=-1 and
    // +0x4c=0.  Keep these semantic mirrors scoped to this common path; the
    // fixed-point impact initializers above intentionally preserve them.
    effect.closest_distance = 0xffffffffu;
    effect.hit_unit_ids.clear();
    if (target != nullptr) {
        InitializeUnitEffectPathToTarget(state, effect, source, *target);
    }
    if (start_kind == UnitEffectActionStartKind::status_chain_path) {
        effect.chain_remaining = chain_count_from_source_status(source);
        effect.chained_target_ids = {};
    }
}

bool BeginUnitEffectImmediate(UnitEffectRuntimeState& state, UnitEffectRuntime& effect,
    u32 effect_id, UnitMovementUnit& source, UnitMovementUnit* target) {
    const UnitEffectDefinition* definition = find_effect_definition(state, effect_id);
    if (definition == nullptr) {
        return false;
    }
    // Low-id action initializers fill only their own raw fields after moving
    // a node out of the free list.  Preserve unwritten payload on reuse.
    effect.active = true;
    effect.effect_id = effect_id;
    effect.source_unit_id = source.id;
    effect.target_unit_id = target != nullptr ? target->id : 0;
    effect.linked_unit_id = effect.target_unit_id;
    effect.x = source.x;
    effect.y = source.y;
    effect.tick = 0;
    effect.frame = 0;
    append_effect_event(state, UnitEffectEventKind::started, effect);
    return true;
}

void QueueUnitEffectStartSoundIfAny(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition != nullptr && definition->start_sound_slot != 0xffffffffu) {
        // FUN_004ed1d0 is the separate start-sound path.  It spatializes from
        // the caller's current ESI unit; it must not inherit FUN_004ef2c6's
        // effect-0x62 world-point exception.
        append_effect_event(state, UnitEffectEventKind::frame_sound, effect,
            effect.target_unit_id, definition->start_sound_slot,
            UnitEffectSoundSpatialKind::source_unit_current_tile,
            find_effect_unit(state, effect.source_unit_id));
    }
}

UnitMovementUnit* FindUnitEffectImpactTarget(UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect) {
    if (UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id)) {
        return target;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const u32 radius = definition != nullptr ? definition->impact_radius : 0;
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    for (UnitMovementUnit& unit : state.units) {
        if (definition != nullptr &&
            !unit_effect_area_candidate_passes_common_gates(
                state, effect, source, unit, *definition)) {
            continue;
        }
        if (effect_unit_inside_impact_area(effect, unit, radius)) {
            return &unit;
        }
    }
    return nullptr;
}

void RenderUnitEffectRuntimeSprite(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr || definition->sprite_entry == 0) {
        return;
    }
    if (!unit_effect_low_id_sprite_blit_allowed(*definition, effect)) {
        return;
    }
    const u32 sprite_entry = effect_sprite_entry_for_frame(state, *definition, effect);
    append_effect_event(state, UnitEffectEventKind::render, effect,
        effect.target_unit_id, sprite_entry);
    queue_effect_render_command(state, effect);
}

bool ResolveUnitEffectGenericSpriteRender(const UnitEffectRuntimeState& state,
    const UnitEffectRuntime& effect, u32& sprite_entry, u32& draw_mode) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr || definition->sprite_entry == 0 ||
        !unit_effect_low_id_sprite_blit_allowed(*definition, effect)) {
        return false;
    }
    sprite_entry = effect_sprite_entry_for_frame(state, *definition, effect);
    if (sprite_entry == 0) {
        return false;
    }
    draw_mode = effect_draw_mode_for_frame(*definition, effect);
    return true;
}

u32 unit_effect_projectile_loop_period(const UnitEffectDefinition* definition) {
    u32 period = definition != nullptr
        ? definition->action_projectile_loop_ticks
        : 1;
    if (definition != nullptr && definition->action_path_control == 1) {
        period >>= 3;
    }
    return std::max<u32>(1, period);
}

void tick_unit_effect_projectile_loop_timer(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    ++effect.tick;
    if (effect.tick >= unit_effect_projectile_loop_period(definition)) {
        effect.tick = 0;
    }
}

bool DispatchUnitEffectCommandPreImpactPhase(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return true;
    }
    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        return false;
    }
    TickUnitEffectLoopOrRefund(state, effect);
    return true;
}

void TickUnitEffectGenericCommandImpactTail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        finish_effect(state, effect);
        return;
    }
    if (frame_in_list(definition->impact_frames, effect.frame)) {
        ApplyUnitEffectAreaDamageByRenderClassMask(state, effect, effect.amount,
            definition->action_area_damage_radius,
            definition->action_area_target_render_class_mask);
    }
    PlayUnitEffectFrameSound(state, effect);
    ++effect.frame;
    ++effect.tick;
    if (effect.frame >= active_frame_count(definition)) {
        finish_effect(state, effect);
    }
}

void DispatchUnitEffectGenericCommandState(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }
    if (UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id)) {
        effect.x = target->x;
        effect.y = target->y;
    }
    TickUnitEffectGenericCommandImpactTail(state, effect);
}

void TickUnitEffectActiveSequenceThenImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        DispatchUnitEffectGenericCommandState(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    ++effect.frame;
    const u32 sequence_count = definition != nullptr
        ? definition->render_ticks
        : 0;
    if (effect.frame < sequence_count) {
        if (definition != nullptr &&
            effect.frame < definition->action_sequence_image_indices.size()) {
            effect.tick = definition->action_sequence_image_indices[effect.frame];
        }
        return;
    }

    effect.flags = kUnitEffectFlagImpact;
    effect.frame = 0;
    effect.tick = 0;
}

void TickUnitEffectSourceAnchoredGenericCommand(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (source == nullptr ||
        (source->runtime_flags & kUnitActionTargetTransient) != 0) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    effect.x = source->x;
    effect.y = source->y;
    DispatchUnitEffectGenericCommandState(state, effect);
}

void TickUnitEffectClearLinkedHiddenAfterCountdown(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }
    if (effect.tick != 0) {
        --effect.tick;
        return;
    }
    if (UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id)) {
        target->runtime_flags &= ~0x80u;
    }
    ReleaseUnitEffectSlot(state, effect);
}

void TickUnitEffectSourceCommandFlagPhase(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 required_command_flag) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (source == nullptr ||
        (source->runtime_flags & kUnitActionTargetTransient) != 0 ||
        (source->command_flags & required_command_flag) == 0) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const u32 period = definition != nullptr
        ? std::max<u32>(1, definition->startup_ticks)
        : 1;
    const u32 slot = source->runtime_slot_index != kInvalidUnitRuntimeSlotIndex
        ? source->runtime_slot_index
        : source->id >= 0x1d0u ? source->id / 0x1d0u : source->id;
    effect.tick = ((slot + state.frame_counter) & 0xfffu) % period;
    effect.x = source->x;
    effect.y = source->y;
}

void TickUnitEffectPathCounterCountdownThenGeneric(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (effect.abs_delta_x != 0) {
        --effect.abs_delta_x;
        return;
    }
    DispatchUnitEffectGenericCommandState(state, effect);
}

void TickUnitEffectRenderClass1TargetCenterImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        DispatchUnitEffectGenericCommandState(state, effect);
        return;
    }
    UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id);
    if (target == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    if (target->definition.render_class == 1) {
        effect.x = action_center_x(*target);
        effect.y = action_center_y(*target);
    } else {
        effect.x = target->x;
        effect.y = target->y;
    }
    TickUnitEffectGenericCommandImpactTail(state, effect);
}

void TickUnitEffectLinkedRuntime100Countdown(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id);
    if (target == nullptr ||
        (target->runtime_flags & kUnitActionTargetTransient) != 0) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    effect.x = target->x;
    effect.y = target->y;
    ++effect.frame;
    ++effect.tick;
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (effect.frame >= active_frame_count(definition)) {
        effect.frame = 0;
        effect.tick = 0;
    }

    if ((state.frame_counter & 1u) != 0) {
        return;
    }
    --effect.amount;
    if (static_cast<i32>(effect.amount) > 0) {
        return;
    }
    target->runtime_flags &= ~0x100u;
    ReleaseUnitEffectSlot(state, effect);
}

void TickUnitEffectImpactFrameSoundTail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        finish_effect(state, effect);
        return;
    }
    PlayUnitEffectFrameSound(state, effect);
    ++effect.frame;
    ++effect.tick;
    if (effect.frame >= active_frame_count(definition)) {
        finish_effect(state, effect);
    }
}

void TickUnitEffectLinkedTargetSacrificeHeal(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }

    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    if (linked != nullptr) {
        effect.x = linked->x;
        effect.y = linked->y;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition != nullptr &&
        frame_in_list(definition->impact_frames, effect.frame)) {
        UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
        if (source != nullptr && linked != nullptr &&
            (source->runtime_flags & kUnitActionTargetTransient) == 0 &&
            (linked->runtime_flags & kUnitActionTargetTransient) == 0) {
            const u32 amount = definition->damage_amount;
            AddUnitHealthClampedToProductionEffect00(
                production_state_or_empty(state), *source, amount);
            linked->command_state |= kUnitCommandDead;
            // Action 0x1b (0x004eedd0) heals the source and marks the linked
            // unit dead, then goes straight to the frame/sound tail.  It does
            // not enqueue a second damage-class impact for the dead target.
        }
    }

    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void release_unit_effects_linked_to_target(UnitEffectRuntimeState& state,
    u32 target_unit_id) {
    const std::vector<std::size_t> active_indices = state.active_effect_indices;
    for (const std::size_t index : active_indices) {
        if (index >= state.effect_slots.size()) {
            continue;
        }
        UnitEffectRuntime& other = state.effect_slots[index];
        if (!other.active || other.linked_unit_id != target_unit_id) {
            continue;
        }
        if (other.effect_id == 0x3f ||
            ((other.effect_id == 0x3d || other.effect_id == 0x4d) &&
                (other.flags & kUnitEffectFlagImpact) != 0)) {
            ReleaseUnitEffectSlot(state, other);
        }
    }
}

void TickUnitEffectClearLinkedRuntime100AndRelatedEffects(
    UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }

    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    if (linked != nullptr) {
        effect.x = linked->x;
        effect.y = linked->y;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const bool first_impact_frame = definition != nullptr &&
        !definition->impact_frames.empty() &&
        effect.frame == definition->impact_frames.front();
    if (first_impact_frame && linked != nullptr) {
        if ((linked->runtime_flags & 0x20u) != 0) {
            linked->command_lockout_ticks = 1;
        }
        if ((linked->runtime_flags & 0x100u) != 0) {
            linked->runtime_flags &= ~0x100u;
            release_unit_effects_linked_to_target(state, linked->id);
        }
    }

    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void TickUnitEffectSourceSecondaryDrainFlag(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (source == nullptr || definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if (effect.tick < definition->startup_ticks) {
        ++effect.tick;
        effect.x = source->x;
        effect.y = source->y;
        return;
    }

    if (source->secondary_value == 0) {
        source->command_flags &= ~0x840u;
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    if ((source->command_flags & 0x800u) == 0) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    ++effect.frame;
    const u32 period = std::max<u32>(1, definition->damage_amount);
    if (effect.frame < period) {
        return;
    }
    --source->secondary_value;
    effect.frame = 0;
}

void TickUnitEffectSourceHealthCostStat20Window(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (source == nullptr || definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if (effect.tick == 0) {
        if (source->health <= definition->action_source_health_cost) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        source->health -= definition->action_source_health_cost;
        source->runtime_stat_20 += definition->action_source_stat20_delta;
    }

    if (effect.tick < definition->startup_ticks) {
        effect.x = source->x;
        effect.y = source->y;
        ++effect.tick;
    }

    if (effect.frame < definition->damage_amount) {
        ++effect.frame;
        return;
    }

    source->runtime_stat_20 -= definition->action_source_stat20_delta;
    ReleaseUnitEffectSlot(state, effect);
}

void TickUnitEffectTargetCommandFlag40Window(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }

    UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id);
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        if (target == nullptr ||
            (target->runtime_flags & kUnitActionTargetTransient) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }

        ++effect.frame;
        if (effect.frame < effect.amount) {
            return;
        }
        if ((target->runtime_flags & 0x10u) != 0 ||
            (target->definition.type_flags & 0x2u) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        if (state.equipment_catalog != nullptr &&
            CalculateUnitEquipmentCommandFlagModifier(
                *target, *state.equipment_catalog) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }

        target->command_flags &= ~0x40u;
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if (target != nullptr) {
        effect.x = target->x;
        effect.y = target->y;
    }
    if (target != nullptr &&
        frame_in_list(definition->impact_frames, effect.frame) &&
        (target->runtime_flags & kUnitActionTargetTransient) == 0) {
        target->command_flags |= 0x40u;
    }

    PlayUnitEffectFrameSound(state, effect);
    ++effect.frame;
    ++effect.tick;
    if (effect.frame < active_frame_count(definition)) {
        return;
    }
    effect.frame = 0;
    effect.flags &= ~kUnitEffectFlagImpact;
}

void TickUnitEffectFirstImpactFrameMarksSourceDead(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const bool mark_source_dead = definition != nullptr &&
        !definition->impact_frames.empty() &&
        effect.frame == definition->impact_frames.front();
    DispatchUnitEffectGenericCommandState(state, effect);
    if (mark_source_dead) {
        if (UnitMovementUnit* source =
                find_effect_unit(state, effect.source_unit_id)) {
            source->command_state |= kUnitCommandDead;
        }
    }
}

void TickUnitEffectRelocateSourceOnFirstImpact(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const bool first_impact_frame = definition != nullptr &&
        !definition->impact_frames.empty() &&
        effect.frame == definition->impact_frames.front();
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (first_impact_frame && source != nullptr &&
        state.lifecycle_context != nullptr &&
        state.lifecycle_context->movement != nullptr) {
        UnitLifecycleContext& lifecycle = *state.lifecycle_context;
        i32 target_x = effect.x;
        i32 target_y = effect.y;
        if (!CheckUnitCanEnterTerrainCell(
                *lifecycle.movement, *source, target_x, target_y) &&
            !FindStrictUnitPlacementPoint(
                lifecycle, *source, target_x, target_y)) {
            TickUnitEffectImpactFrameSoundTail(state, effect);
            return;
        }

        ClearUnitFootprintOccupancyBits(lifecycle, *source);
        source->x = target_x;
        source->y = target_y;
        source->destination_x = target_x;
        source->destination_y = target_y;
        source->command_flags &= ~0x1000u;
        effect.x = target_x;
        effect.y = target_y;
        SetUnitFootprintOccupancyBits(lifecycle, *source);
    }

    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void TickUnitEffectMidpointChildHealthDrain(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        ++effect.frame;
        ++effect.tick;
        const u32 active_lifetime = definition->render_ticks;
        if (effect.tick >= active_lifetime) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        if (effect.frame != (active_lifetime >> 1)) {
            return;
        }

        UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
        const u32 radius = definition->action_area_damage_radius;
        bool stop_scan = false;
        for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
            if (stop_scan ||
                !unit.active || (unit.runtime_flags & 0x20000084u) != 0 ||
                (unit.definition.action_effect_flags & 0x10u) == 0 ||
                unit.definition.render_class >= 32 ||
                (definition->action_area_target_render_class_mask &
                    (1u << unit.definition.render_class)) == 0) {
                return;
            }
            const u32 distance = CalculateApproxUnitDistance(
                effect.x, effect.y, unit.x, unit.y);
            if (distance > radius) {
                return;
            }
            UnitEffectRuntime* child = AllocateUnitEffectSlot(state);
            if (child == nullptr) {
                stop_scan = true;
                return;
            }
            child->effect_id = 0x4d;
            child->flags = kUnitEffectFlagImpact;
            child->tick = 0;
            child->frame = 0;
            child->source_unit_id = source != nullptr ? source->id : effect.source_unit_id;
            child->target_unit_id = unit.id;
            child->linked_unit_id = unit.id;
            child->x = unit.x;
            child->y = unit.y;
        });
        return;
    }

    UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id);
    if (target == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    const u32 percent = definition->damage_amount;
    const u32 threshold = static_cast<u32>(
        (static_cast<u64>(target->max_health) * percent) / 100u);
    if (target->health <= threshold) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    --target->health;

    ++effect.tick;
    if (effect.tick >= active_frame_count(definition)) {
        effect.tick = 0;
    }
    effect.x = target->x;
    effect.y = target->y;
    PlayUnitEffectFrameSound(state, effect);
}

void TickUnitEffectTimedCommandMarkerChildren(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    constexpr u32 kChildMarkerEffectFlag = 0x800u;
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }

    if ((effect.flags & kChildMarkerEffectFlag) != 0) {
        UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
        if (linked == nullptr) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        if ((linked->runtime_flags & kUnitActionTargetTransient) != 0) {
            linked->command_flags &= ~0x1000u;
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        ++effect.frame;
        if (effect.frame < effect.amount) {
            return;
        }
        linked->command_flags &= ~0x1000u;
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    ++effect.frame;
    if (effect.frame >= effect.amount) {
        ++effect.tick;
        if (effect.tick >= active_frame_count(definition)) {
            ReleaseUnitEffectSlot(state, effect);
        }
        return;
    }

    ++effect.tick;
    if (effect.tick > definition->action_aura_tick_reset_threshold) {
        effect.tick = definition->action_aura_tick_reset_value;
    }
    if ((effect.frame & 7u) != 0) {
        return;
    }

    bool stop_scan = false;
    for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
        if (stop_scan ||
            (unit.command_flags & 0x1000u) != 0 ||
            (unit.runtime_flags & 0x20000080u) != 0 ||
            unit.definition.render_class != 0 ||
            unit.definition.projectile_impact_class == 2) {
            return;
        }
        const u32 distance = CalculateApproxUnitDistance(
            effect.x, effect.y, unit.x, unit.y);
        if (distance > definition->action_area_damage_radius) {
            return;
        }

        UnitEffectRuntime* child = AllocateUnitEffectSlot(state);
        if (child == nullptr) {
            stop_scan = true;
            return;
        }
        child->effect_id = effect.effect_id;
        child->flags = kChildMarkerEffectFlag;
        child->source_unit_id = effect.source_unit_id;
        child->target_unit_id = unit.id;
        child->linked_unit_id = unit.id;
        child->frame = effect.frame;
        child->amount = effect.amount;
        unit.command_flags |= 0x1000u;
    });
}

void TickUnitEffectImpactCreatePlacedUnit(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition != nullptr &&
        frame_in_list(definition->impact_frames, effect.frame)) {
        UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
        if (source != nullptr) {
            UnitMovementUnit* created = nullptr;
            if (state.callbacks.create_unit != nullptr) {
                const u32 type_id = definition->action_create_unit_type_id & 0xffu;
                created = state.callbacks.create_unit(
                    state, *source, type_id, effect.x, effect.y);
            }
            if (created == nullptr) {
                const u64 restored =
                    static_cast<u64>(source->secondary_value) +
                    definition->action_create_unit_refund_secondary;
                source->secondary_value =
                    restored > source->max_secondary_value
                        ? source->max_secondary_value
                        : static_cast<u32>(restored);
            } else {
                created->max_secondary_value =
                    definition->action_create_unit_secondary_value;
                created->secondary_value =
                    definition->action_create_unit_secondary_value;
                created->health = created->max_health;
            }
        }
    }

    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void TickUnitEffectTargetStatusMarkerWait(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    constexpr u32 kMarkerWaitFlag = 0x200u;
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
        if (source == nullptr ||
            (source->runtime_flags & kUnitActionTargetTransient) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        const u32 command_id = GetUnitCommandIdLow24(*source);
        if (command_id != 0x7e && command_id != 0x5b) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        effect.x = source->x;
        effect.y = source->y;
        ++effect.tick;
        if (definition != nullptr &&
            effect.tick >= definition->startup_ticks) {
            effect.tick = 0;
        }
        return;
    }

    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
        if (linked == nullptr ||
            (linked->runtime_flags & kUnitActionTargetTransient) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
        // FUN_004eddbb reads linked raw +0x30.  Action 0x27 tracks the
        // structure construction gate stored there for type-0x60+ objects;
        // raw +0x54 is the unrelated status/level timer.
        if (linked->action_mode_gate == 1) {
            ++effect.tick;
            if (definition != nullptr &&
                effect.tick >= active_frame_count(definition)) {
                effect.tick = 0;
            }
            return;
        }
        effect.tick = 0;
        effect.flags = kMarkerWaitFlag;
        return;
    }

    if ((effect.flags & kMarkerWaitFlag) != 0) {
        ++effect.tick;
        if (effect.tick >= 0x3eu) {
            ReleaseUnitEffectSlot(state, effect);
        }
        return;
    }

    ReleaseUnitEffectSlot(state, effect);
}

void TickUnitEffectMarkNearbyActionFlagUnits(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    --effect.abs_delta_x;
    if (effect.abs_delta_x == 0) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    const u32 radius = definition->action_nearby_marker_radius;
    for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
        if (!unit.active ||
            (unit.definition.action_effect_flags & 0x200u) == 0 ||
            unit.definition.render_class == 3) {
            return;
        }
        const u32 distance =
            CalculateApproxUnitDistance(effect.x, effect.y, unit.x, unit.y);
        if (distance <= radius) {
            unit.runtime_flags |= 0xc00u;
        }
    });

    ++effect.frame;
    ++effect.tick;
    if (effect.frame < active_frame_count(definition)) {
        return;
    }
    effect.frame = 0;
    effect.tick = 0;
}

void TickUnitEffectPeriodicType31HealAura(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    if (effect.frame < definition->action_aura_frame_limit) {
        ++effect.frame;
        ++effect.amount;
        if (effect.amount >= definition->damage_amount) {
            effect.amount = 0;
            UnitMovementUnit* source =
                find_effect_unit(state, effect.source_unit_id);
            if (source == nullptr) {
                ReleaseUnitEffectSlot(state, effect);
                return;
            }
            const u32 relation_mask = source_owner_relation_mask(state, *source);
            const u32 radius = definition->action_aura_radius;
            for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
                if (!unit.active || unit.type_id != 0x31 ||
                    (unit.runtime_flags & 0x84u) != 0 ||
                    !unit_owner_bit_set(relation_mask, unit)) {
                    return;
                }
                const u32 distance =
                    CalculateApproxUnitDistance(effect.x, effect.y, unit.x, unit.y);
                if (distance <= radius) {
                    AddUnitHealthClampedToProductionEffect00(
                        production_state_or_empty(state), unit, 1);
                }
            });
        }
    }

    if (effect.tick >= definition->action_aura_tick_reset_threshold) {
        effect.tick = definition->action_aura_tick_reset_value - 1u;
    }
    ++effect.tick;
    if (effect.tick >= active_frame_count(definition)) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    PlayUnitEffectFrameSound(state, effect);
}

void TickUnitEffectSecondaryAreaDamagePulse(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    if (linked != nullptr) {
        effect.x = linked->x;
        effect.y = linked->y;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (linked != nullptr && definition != nullptr &&
        (linked->runtime_flags & kUnitActionTargetTransient) == 0 &&
        frame_in_list(definition->impact_frames, effect.frame)) {
        if (UnitMovementUnit* source =
                find_effect_unit(state, effect.source_unit_id)) {
            const u32 debit = std::min(
                source->secondary_value,
                definition->action_secondary_area_debit_limit);
            source->secondary_value -= debit;
            const u32 impact_class =
                std::min<u32>(linked->definition.projectile_impact_class, 2);
            const u32 percent =
                definition->action_projectile_impact_percent[impact_class];
            const u32 amount = static_cast<u32>(
                (static_cast<u64>(debit) * percent) / 100u);
            ApplyUnitEffectAreaDamageByRenderClassMask(state, effect, amount,
                definition->action_area_damage_radius,
                definition->action_area_target_render_class_mask);
        }
    }

    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void TickUnitEffectChanneledLinkedHealthPulse(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        TickUnitEffectStartupTimer(state, effect);
        return;
    }
    if ((effect.flags & kUnitEffectFlagImpact) == 0) {
        TickUnitEffectLoopOrRefund(state, effect);
        return;
    }

    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    auto release_and_clear_source_flag = [&]() {
        if (source != nullptr) {
            source->command_flags &= ~0x2000u;
        }
        ReleaseUnitEffectSlot(state, effect);
    };

    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (source == nullptr || linked == nullptr || definition == nullptr ||
        (source->runtime_flags & kUnitActionTargetTransient) != 0 ||
        (linked->runtime_flags & kUnitActionTargetTransient) != 0 ||
        source->health <= 1 ||
        ResolveUnitRuntimeStateFromCommandTable(
            *source, &state.command_state_table) != 1) {
        release_and_clear_source_flag();
        return;
    }

    effect.x = source->x;
    effect.y = source->y;
    if ((default_action_distance(*source, *linked) >> 1) >
        definition->action_area_damage_radius) {
        release_and_clear_source_flag();
        return;
    }

    ++effect.frame;
    if (effect.frame >= definition->damage_amount) {
        effect.frame = 0;
        if (apply_unit_effect_raw_health_damage(state, *source, 1)) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }
    }

    ++effect.amount;
    if (effect.amount >= definition->action_channel_linked_damage_period) {
        effect.amount = 0;
        if (apply_unit_effect_raw_health_damage(state, *linked, 1)) {
            release_and_clear_source_flag();
            return;
        }
    }

    PlayUnitEffectFrameSound(state, effect);
    ++effect.tick;
    if (effect.tick >= active_frame_count(definition)) {
        effect.tick = 0;
    }
}

void TickUnitEffectSourceCommandStateGate(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (source == nullptr ||
        (source->runtime_flags & kUnitActionTargetTransient) != 0) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    const UnitMovementPoint source_delta = unit_effect_source_offset(*source);
    effect.x = source->x + source_delta.x;
    effect.y = source->y + source_delta.y;

    const u32 command_id = GetUnitCommandIdLow24(*source);
    bool keep_effect = false;
    switch (effect.flags) {
    case 0:
        keep_effect = command_id == 0x5b &&
            (source->target != nullptr
                ? source->target->id == effect.linked_unit_id
                : effect.linked_unit_id == 0);
        break;
    case 1:
        keep_effect = command_id == 0x54;
        break;
    case 2:
        keep_effect = command_id == 0x7e;
        break;
    default:
        keep_effect = false;
        break;
    }
    if (!keep_effect) {
        ReleaseUnitEffectSlot(state, effect);
    }
}

void TickUnitEffectRestoreLinkedTargetHealth(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        if (UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id)) {
            effect.x = source->x;
            effect.y = source->y;
        }
        TickUnitEffectStartupTimer(state, effect);
        return;
    }

    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const u32 secondary_cost = state.linked_health_restore_globals_loaded
        ? state.linked_health_restore_secondary_cost
        : definition != nullptr ? definition->action_secondary_cost : 0;
    const u32 restore_amount = state.linked_health_restore_globals_loaded
        ? state.linked_health_restore_amount
        : definition != nullptr && definition->action_target_health_delta > 0
            ? static_cast<u32>(definition->action_target_health_delta)
            : 0;
    if (source != nullptr && linked != nullptr && (state.frame_counter & 1u) == 0 &&
        source->secondary_value >= secondary_cost &&
        (linked->runtime_flags & kUnitActionTargetTransient) == 0) {
        const u32 target_max =
            CalculateUnitRuntimeMaxHealthWithProductionEffect00(
                production_state_or_empty(state), *linked);
        if (linked->health < target_max) {
            source->secondary_value -= secondary_cost;
            linked->health =
                std::min<u32>(target_max, linked->health + restore_amount);
            // Original action-0x28 handler 0x004ee21c mutates HP directly and
            // then jumps to the generic frame/sound tail.  It does not publish
            // a damage-class impact; doing so makes the shared impact drain
            // immediately damage the target for the amount just restored.
        }
    }

    if (linked != nullptr) {
        effect.x = linked->x;
        effect.y = linked->y;
    }
    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void TickUnitEffectTransferSecondaryToLinkedTarget(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    if (source != nullptr && linked != nullptr && source->secondary_value != 0 &&
        (linked->runtime_flags & kUnitActionTargetTransient) == 0) {
        const u32 target_max =
            CalculateUnitRuntimeMaxSecondaryValueWithProductionEffect01(
                production_state_or_empty(state), *linked);
        if (linked->secondary_value < target_max) {
            --source->secondary_value;
            ++linked->secondary_value;
        }
    }

    if (linked != nullptr) {
        effect.x = linked->x;
        effect.y = linked->y;
    }
    TickUnitEffectImpactFrameSoundTail(state, effect);
}

UnitMovementUnit* find_reserved_tile_completion_dropoff(UnitEffectRuntimeState& state,
    UnitMovementUnit& source) {
    if (state.lifecycle_context != nullptr &&
        state.lifecycle_context->movement != nullptr) {
        return FindNearestOwnedDropoffBuilding(
            *state.lifecycle_context->movement, source).unit;
    }

    UnitMovementUnit* best = nullptr;
    u32 best_distance = 0xffffffffu;
    for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
        // Lifecycle-less fallback mirrors FindNearestOwnedDropoffBuilding
        // (0x004c6feb): raw +0x30 is the construction gate.
        if (&unit == &source || !unit.active || unit.owner_id != source.owner_id ||
            unit.action_mode_gate == 1) {
            return;
        }
        if (unit.type_id != 0x60 && unit.type_id != 0x70 &&
            unit.type_id != 0x80 && unit.type_id != 0x90) {
            return;
        }
        const u32 distance = default_action_distance(source, unit);
        if (distance <= best_distance) {
            best = &unit;
            best_distance = distance;
        }
    });
    return best;
}

void initialize_reserved_tile_completion_path(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, UnitMovementUnit& source, UnitMovementUnit& target,
    i32 start_x, i32 start_y) {
    effect.source_unit_id = source.id;
    effect.target_unit_id = target.id;
    effect.linked_unit_id = target.id;
    effect.x = start_x;
    effect.y = start_y;
    effect.target_x = action_center_x(target);
    effect.target_y = action_center_y(target);
    effect.delta_x = effect.target_x - effect.x;
    effect.delta_y = effect.target_y - effect.y;
    effect.previous_x = effect.x;
    effect.previous_y = effect.y;
    effect.frame = 0xffffffffu;
    effect.tick = 0;
    effect.range = 0xffffffffu;
    effect.closest_distance = 0xffffffffu;
    configure_projectile_step_fields(state, effect);
    // Berry-fly initializer 0x004efc54 writes raw flags to zero and then sets
    // only the Bresenham Y-major bit at 0x004efc63.  Do not retain the startup
    // bit from the preceding attachment reservation.
    effect.flags &= kUnitEffectFlagProjectileYMajor;
}

bool advance_reserved_tile_completion_path(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    // FUN_004f082d advances one Bresenham step and completes on the first
    // point inside the linked unit's bounds.  It does not run the generic
    // closest-distance/range/rewind projectile path.
    if (AdvanceUnitEffectPathStepAndCheckTargetBounds(state, effect)) {
        return true;
    }
    tick_unit_effect_projectile_loop_timer(state, effect);
    return false;
}

void credit_reserved_tile_completion_resources(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, const UnitMovementUnit& source) {
    const u32 owner = source.owner_id;
    if (owner < state.owner_primary_resources.size()) {
        state.owner_primary_resources[owner] += effect.amount;
    }
    if (owner < state.owner_resource_score.size()) {
        state.owner_resource_score[owner] += effect.amount;
    }
    if (owner < state.owner_primary_resources.size()) {
        append_effect_event(state, UnitEffectEventKind::refunded, effect,
            source.id, effect.amount);
    }
}

void finish_reserved_tile_completion_path(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, bool deposit_amount) {
    if (deposit_amount) {
        UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
        if (source != nullptr) {
            credit_reserved_tile_completion_resources(state, effect, *source);
        }
        effect.source_unit_id = 0;
    }
    ReleaseUnitEffectSlot(state, effect);
}

void tick_reserved_tile_completion_active_loop(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const bool reached = advance_reserved_tile_completion_path(state, effect);
    if (reached) {
        finish_reserved_tile_completion_path(
            state, effect, (effect.flags & kUnitEffectFlagRefundOnFinish) == 0);
    }
}

void enter_reserved_tile_completion_delayed_release(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    effect.flags |= kUnitEffectFlagRefundOnFinish;
    effect.amount = 0x1f;
    tick_reserved_tile_completion_active_loop(state, effect);
}

void TickUnitEffectReservedTileCompletionDropoff(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if ((effect.flags & kUnitEffectFlagStartup) != 0) {
        UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
        if (source == nullptr ||
            (source->runtime_flags & kUnitActionTargetTransient) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return;
        }

        const u32 command_id = GetUnitCommandIdLow24(*source);
        if (command_id != kUnitStateReservedTileWork) {
            if (effect.closest_distance == 1 ||
                command_id != kUnitStateReservedTileLinkedObject) {
                ReleaseUnitEffectSlot(state, effect);
                return;
            }
            effect.x = source->x;
            effect.y = source->y;
        }

        ++effect.tick;
        if (definition != nullptr &&
            effect.tick >= definition->action_startup_ticks) {
            effect.tick = 0;
        }
        return;
    }

    if ((effect.flags & kUnitEffectFlagImpact) != 0) {
        UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
        if (linked != nullptr) {
            effect.x = linked->x;
            effect.y = linked->y;
        }
        if (definition != nullptr &&
            frame_in_list(definition->impact_frames, effect.frame)) {
            ApplyUnitEffectAreaDamageByRenderClassMask(state, effect, effect.amount,
                definition->action_area_damage_radius,
                definition->action_area_target_render_class_mask);
        }
        TickUnitEffectImpactFrameSoundTail(state, effect);
        return;
    }

    if ((effect.flags & kUnitEffectFlagRefundOnFinish) != 0) {
        --effect.amount;
        if (effect.amount != 0) {
            tick_reserved_tile_completion_active_loop(state, effect);
            return;
        }
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (source == nullptr ||
        (source->runtime_flags & kUnitActionTargetTransient) != 0 ||
        GetUnitCommandIdLow24(*source) != kUnitStateReservedTileLinkedObject) {
        enter_reserved_tile_completion_delayed_release(state, effect);
        return;
    }

    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    if (linked == nullptr ||
        (linked->runtime_flags & kUnitActionTargetTransient) == 0) {
        tick_reserved_tile_completion_active_loop(state, effect);
        return;
    }

    UnitMovementUnit* dropoff =
        find_reserved_tile_completion_dropoff(state, *source);
    if (dropoff == nullptr) {
        enter_reserved_tile_completion_delayed_release(state, effect);
        return;
    }

    const i32 start_x = effect.x;
    const i32 start_y = effect.y;
    initialize_reserved_tile_completion_path(
        state, effect, *source, *dropoff, start_x, start_y);
}

void DispatchUnitActionEffectCommand(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 action_id) {
    switch (action_id) {
    case 0:
        TickUnitEffectLinkedTargetFrames(state, effect);
        return;
    case 1:
        TickUnitEffectRenderClass1TargetCenterImpact(state, effect);
        return;
    case 2:
        TickUnitEffectLinkedRuntime100Countdown(state, effect);
        return;
    case 3:
        TickUnitEffectMarkNearbyActionFlagUnits(state, effect);
        return;
    case 4:
        TickUnitEffectRelocateSourceOnFirstImpact(state, effect);
        return;
    case 5:
        TickUnitEffectAreaStunFrames(state, effect);
        return;
    case 7:
        return;
    case 9:
        TickUnitEffectPathCounterCountdownThenGeneric(state, effect);
        return;
    case 0x0b:
        TickUnitEffectTargetLockFrames(state, effect);
        return;
    case 0x0c:
        TickUnitEffectFirstImpactFrameMarksSourceDead(state, effect);
        return;
    case 0x0d:
        TickUnitEffectAreaDamageFrames(state, effect);
        return;
    case 0x10:
        TickUnitEffectMidpointChildHealthDrain(state, effect);
        return;
    case 0x11:
        TickUnitEffectClearLinkedRuntime100AndRelatedEffects(state, effect);
        return;
    case 0x12:
        TickUnitEffectSourceHealthCostStat20Window(state, effect);
        return;
    case 0x13:
        TickUnitEffectSourceSecondaryDrainFlag(state, effect);
        return;
    case 0x14:
        TickUnitEffectTargetCommandFlag40Window(state, effect);
        return;
    case 0x15:
        TickUnitEffectSecondaryAreaDamagePulse(state, effect);
        return;
    case 0x16:
        TickUnitEffectTimedCommandMarkerChildren(state, effect);
        return;
    case 0x17:
        TickUnitEffectImpactCreatePlacedUnit(state, effect);
        return;
    case 0x18:
        TickUnitEffectChanneledLinkedHealthPulse(state, effect);
        return;
    case 0x19:
    case 0x1c:
        TickUnitEffectClearLinkedHiddenAfterCountdown(state, effect);
        return;
    case 0x1a:
        TickUnitEffectPeriodicType31HealAura(state, effect);
        return;
    case 0x1b:
        TickUnitEffectLinkedTargetSacrificeHeal(state, effect);
        return;
    case 0x1e:
    case 0x1f:
        ReleaseUnitEffectSlot(state, effect);
        return;
    case 0x20:
        TickUnitEffectSourceCommandFlagPhase(state, effect, 0x4000u);
        return;
    case 0x21:
        TickUnitEffectSourceCommandFlagPhase(state, effect, 0x8000u);
        return;
    case 0x22:
        TickUnitEffectSourceCommandFlagPhase(state, effect, 0x10000u);
        return;
    case 0x23:
        TickUnitEffectSourceCommandFlagPhase(state, effect, 0x20000u);
        return;
    case 0x25:
        TickUnitEffectActiveSequenceThenImpact(state, effect);
        return;
    case 0x26:
        TickUnitEffectReservedTileCompletionDropoff(state, effect);
        return;
    case 0x27:
        TickUnitEffectTargetStatusMarkerWait(state, effect);
        return;
    case 0x28:
        TickUnitEffectRestoreLinkedTargetHealth(state, effect);
        return;
    case 0x2a:
        TickUnitEffectTransferSecondaryToLinkedTarget(state, effect);
        return;
    case 0x2c:
        TickUnitEffectSourceCommandStateGate(state, effect);
        return;
    case 0x2d:
        TickUnitEffectSourceAnchoredGenericCommand(state, effect);
        return;
    default:
        DispatchUnitEffectGenericCommandState(state, effect);
        return;
    }
}

void TickUnitEffectFrameAndApplyImpacts(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        finish_effect(state, effect);
        return;
    }
    if (effect.effect_id >= 0x3du) {
        if (UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id)) {
            effect.x = linked->x;
            effect.y = linked->y;
        }
    }
    const bool low_id_tick_frame = effect_uses_tick_animation_frame(effect);
    const u32 animation_frame = effect_animation_frame(effect);
    if (frame_in_list(definition->impact_frames, animation_frame)) {
        if (effect.effect_id < 0x3du) {
            UnitMovementUnit* source =
                find_effect_unit(state, effect.source_unit_id);
            if (UnitMovementUnit* target =
                    find_effect_unit(state, effect.target_unit_id)) {
                const u32 amount = unit_effect_point_impact_damage(
                    state, effect, source, *target);
                append_effect_event(state, UnitEffectEventKind::impact, effect,
                    target->id, amount);
            }
        } else {
            ApplyUnitEffectAreaDamageByRenderClassMask(state, effect, effect.amount,
                definition->action_area_damage_radius,
                definition->action_area_target_render_class_mask);
        }
    }
    PlayUnitEffectFrameSound(state, effect);
    if (low_id_tick_frame) {
        ++effect.tick;
    } else {
        ++effect.tick;
        ++effect.frame;
    }
    // Generic low-id impact state 0x004ecca2 compares raw +0x0c against
    // JW2_12 +0x228. `impact_render_ticks` mirrors that field; +0x224 is the
    // separate active-path animation period and must not extend the impact.
    const u32 frame_limit = low_id_tick_frame
        ? impact_render_ticks(definition)
        : active_frame_count(definition);
    if (effect_animation_frame(effect) >= frame_limit) {
        finish_effect(state, effect);
    }
}

void TickUnitEffectLoopOrRefund(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (find_effect_definition(state, effect.effect_id) == nullptr) {
        finish_effect(state, effect);
        return;
    }
    const bool reached_target_bounds =
        AdvanceUnitEffectPathStepAndCheckTargetBounds(state, effect);
    if (!reached_target_bounds) {
        tick_unit_effect_projectile_loop_timer(state, effect);
        return;
    }

    if (effect.effect_id != 0x63u) {
        return;
    }
    if ((effect.flags & kUnitEffectFlagRefundOnFinish) == 0) {
        if (UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id)) {
            credit_reserved_tile_completion_resources(state, effect, *source);
        }
        effect.source_unit_id = 0;
    }
    ReleaseUnitEffectSlot(state, effect);
}

void TickUnitEffectStartupTimer(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    ++effect.tick;
    if (definition == nullptr || effect.tick >= startup_timer_limit(definition)) {
        finish_effect(state, effect);
    }
}

void PlayUnitEffectFrameSound(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        return;
    }
    const u32 animation_frame = effect_animation_frame(effect);
    for (const auto& frame_sound : definition->frame_sound_slots) {
        if (frame_sound.first == animation_frame) {
            // FUN_004ef2c6 reads raw +0x1c (the target/later-linked alias) for
            // every effect except 0x62.  Only 0x62 spatializes from raw
            // +0x20/+0x24, so retain both the alias identity and point.
            append_effect_event(state, UnitEffectEventKind::frame_sound, effect,
                effect.linked_unit_id, frame_sound.second,
                effect.effect_id == 0x62
                    ? UnitEffectSoundSpatialKind::world_point
                    : UnitEffectSoundSpatialKind::linked_unit_current_tile,
                effect.effect_id == 0x62
                    ? nullptr
                    : find_effect_unit(state, effect.linked_unit_id));
            return;
        }
    }
    if (frame_in_list(definition->sound_frames, animation_frame)) {
        append_effect_event(state, UnitEffectEventKind::frame_sound, effect,
            effect.linked_unit_id, animation_frame,
            effect.effect_id == 0x62
                ? UnitEffectSoundSpatialKind::world_point
                : UnitEffectSoundSpatialKind::linked_unit_current_tile,
            effect.effect_id == 0x62
                ? nullptr
                : find_effect_unit(state, effect.linked_unit_id));
    }
}

void TickUnitEffectLinkedTargetFrames(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }

    UnitMovementUnit* target = find_effect_unit(state, effect.linked_unit_id);
    if (target == nullptr || target->health <= 1) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    --target->health;
    effect.x = target->x;
    effect.y = target->y;

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    PlayUnitEffectFrameSound(state, effect);
    ++effect.frame;
    ++effect.tick;
    if (effect.frame >= active_frame_count(definition)) {
        effect.frame = 0;
        effect.tick = 0;
    }
}

void TickUnitEffectAreaStunFrames(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }
    const u32 radius = definition->impact_radius;
    if ((effect.frame & 3u) == 0) {
        for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
            if (!unit_effect_area_stun_candidate_allowed(effect, unit, radius)) {
                return;
            }
            apply_unit_effect_area_stun_side_effect(unit);
            if (definition->start_sound_slot != 0xffffffffu) {
                UnitEffectEvent event{};
                event.kind = UnitEffectEventKind::frame_sound;
                event.sound_spatial =
                    UnitEffectSoundSpatialKind::linked_unit_current_tile;
                event.effect_id = effect.effect_id;
                event.unit_id = effect.source_unit_id;
                event.target_id = unit.id;
                event.value = definition->start_sound_slot;
                event.x = unit.x;
                event.y = unit.y;
                state.events.push_back(event);
            }
        });
    }

    ++effect.frame;
    ++effect.tick;
    if (effect.frame >= active_frame_count(definition)) {
        effect.frame = 0;
        effect.tick = 0;
    }
    ++effect.abs_delta_x;
    if (effect.abs_delta_x >= definition->damage_amount) {
        ReleaseUnitEffectSlot(state, effect);
    }
}

void TickUnitEffectTargetLockFrames(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }

    UnitMovementUnit* linked = find_effect_unit(state, effect.linked_unit_id);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (linked == nullptr || definition == nullptr) {
        ReleaseUnitEffectSlot(state, effect);
        return;
    }

    effect.x = linked->x;
    effect.y = linked->y;
    if (effect.frame == definition->action_target_lock_frame) {
        linked->runtime_flags |= 0x20u;
        const u32 lockout_ticks = effect.amount + 1u;
        if (lockout_ticks != 0) {
            linked->command_state |= 0x40000000u;
            linked->animation_timer = 0;
            linked->command_entry_lockout_ticks = lockout_ticks;
        }
    }
    TickUnitEffectImpactFrameSoundTail(state, effect);
}

void TickUnitEffectAreaDamageFrames(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    if (DispatchUnitEffectCommandPreImpactPhase(state, effect)) {
        return;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        finish_effect(state, effect);
        return;
    }
    if (frame_in_list(definition->impact_frames, effect.frame)) {
        ApplyUnitEffectAreaDamageByUnitFlagMask(state, effect, effect.amount,
            definition->action_area_damage_radius, 0x100u);
    }

    PlayUnitEffectFrameSound(state, effect);
    ++effect.frame;
    ++effect.tick;
    if (effect.frame >= active_frame_count(definition)) {
        effect.frame = 0;
        effect.tick = 0;
    }
    --effect.abs_delta_x;
    if (effect.abs_delta_x == 0) {
        finish_effect(state, effect);
    }
}

bool BeginSelectedUnitAttachmentEffect(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 effect_id, UnitMovementUnit& source,
    UnitMovementUnit* attachment) {
    static_cast<void>(attachment);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect_id);
    if (definition == nullptr || definition->startup_ticks == 0) {
        return false;
    }

    // FUN_004ef6cd writes only raw +0x00, +0x08, +0x0c, +0x10, +0x18,
    // +0x20 and +0x24.  The original effect allocator/releaser only relinks
    // raw +0xa0/+0xa4, so every other payload word intentionally survives a
    // slot reuse.  In particular the first reserved-tile attachment can retain
    // the preceding berry-dropoff direction, amount and target metadata.
    effect.active = true;
    effect.effect_id = effect_id;
    effect.flags = kUnitEffectFlagStartup;
    effect.tick = 0;
    effect.frame = 0;
    effect.source_unit_id = source.id;
    effect.x = source.x;
    effect.y = source.y;
    // FUN_004ef6cd reads raw JW2_11 +0xabc here.  +0x1fc is the unrelated
    // action path-control field used by the later executing initializer.
    if (definition->startup_uses_source_muzzle) {
        const UnitMovementPoint delta = unit_effect_source_offset(source);
        effect.x += delta.x;
        effect.y += delta.y;
    }
    append_effect_event(state, UnitEffectEventKind::started, effect);
    return true;
}

bool StartSelectedUnitAttachmentEffect(UnitEffectRuntimeState& state,
    u32 effect_id, UnitMovementUnit& source, UnitMovementUnit* attachment,
    UnitEffectRuntime** created_effect) {
    if (created_effect != nullptr) {
        *created_effect = nullptr;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect_id);
    if (definition == nullptr) {
        return false;
    }

    // 0x004ef708..0x004ef72e queues +0x834 before checking raw +0x220
    // and before FUN_004f3490 reserves a pool node.  Consequently a zero-tick
    // attachment and a pool-full failure both still make the start sound.
    UnitEffectRuntime sound_effect{};
    sound_effect.effect_id = effect_id;
    sound_effect.source_unit_id = source.id;
    sound_effect.x = source.x;
    sound_effect.y = source.y;
    QueueUnitEffectStartSoundIfAny(state, sound_effect);
    if (definition->startup_ticks == 0) {
        return true;
    }

    UnitEffectRuntime* effect = AllocateUnitEffectSlot(state);
    if (effect == nullptr) {
        return false;
    }
    if (!BeginSelectedUnitAttachmentEffect(
            state, *effect, effect_id, source, attachment)) {
        ReleaseUnitEffectSlot(state, *effect);
        return false;
    }
    if (created_effect != nullptr) {
        *created_effect = effect;
    }
    return true;
}

bool DispatchSelectedUnitScatterActionEffect(UnitEffectRuntimeState& state,
    u32 action_id, UnitMovementUnit& source, i32 world_x, i32 world_y,
    UnitEffectRandomLimitFunction random_limit, void* random_user_data) {
    if (action_id != 9 && action_id != 0x0f) {
        return false;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, action_id + 0x3du);
    if (definition == nullptr) {
        return false;
    }

    const auto cost_available = [&]() {
        return source.secondary_value >= definition->action_secondary_cost &&
            source.health > definition->action_source_health_cost;
    };
    const auto debit_cost = [&]() {
        source.secondary_value -= definition->action_secondary_cost;
        source.health -= definition->action_source_health_cost;
    };
    const auto roll = [&](u32 limit) {
        if (limit == 0 || random_limit == nullptr) {
            return 0u;
        }
        return random_limit(limit, random_user_data) % limit;
    };

    UnitMovementMap* map = state.lifecycle_context != nullptr &&
            state.lifecycle_context->movement != nullptr
        ? &state.lifecycle_context->movement->map
        : nullptr;

    if (action_id == 9) {
        u32 remaining = definition->action_create_unit_secondary_value;
        // Meteo count zero returns carry and never pays the action cost.
        if (remaining == 0 || !cost_available()) {
            return false;
        }
        debit_cost();

        UnitEffectRuntime* reserved = nullptr;
        while (remaining != 0) {
            if (reserved == nullptr) {
                reserved = AllocateUnitEffectSlot(state);
                if (reserved == nullptr) {
                    // 0x004ef8bd: the debit precedes the first and every later
                    // allocation failure and is not rolled back.
                    return false;
                }
            }

            --remaining;
            const u32 counter_limit = definition->action_create_unit_type_id;
            const u32 spread = definition->action_source_stat20_delta;
            const u32 counter = (counter_limit == 0 ? 0u : roll(counter_limit)) + 1u;
            const i32 x = world_x + static_cast<i32>(roll(spread)) -
                static_cast<i32>(spread >> 1);
            const bool x_inside = x >= 0 && map != nullptr &&
                static_cast<u32>(x >> 5) < map->width;
            i32 y = world_y;
            bool inside = false;
            if (x_inside) {
                // The original does not consume the Y RNG call when X is
                // already outside the map.
                y = world_y + static_cast<i32>(roll(spread)) -
                    static_cast<i32>(spread >> 1);
                inside = y >= 0 && static_cast<u32>(y >> 5) < map->height;
            }
            if (!inside) {
                // Invalid candidates reuse this same reserved node.  Only the
                // final exhausted attempt returns it to the pool.
                if (remaining == 0) {
                    ReleaseUnitEffectSlot(state, *reserved);
                }
                continue;
            }

            // Original Meteo child setup writes only the raw effect fields
            // touched at 0x004ef8c4..0x004ef974.  Recycled direction and
            // target-coordinate payload deliberately survives allocation.
            reserved->effect_id = 0x46u;
            reserved->amount = definition->damage_amount;
            reserved->flags = kUnitEffectFlagImpact;
            reserved->tick = 0;
            reserved->frame = 0;
            reserved->source_unit_id = source.id;
            reserved->target_unit_id = 0;
            reserved->linked_unit_id = 0;
            reserved->x = x;
            reserved->y = y;
            reserved->abs_delta_x = counter;
            append_effect_event(state, UnitEffectEventKind::started, *reserved);
            reserved = nullptr;
        }
        return true;
    }

    const u32 count = definition->action_create_unit_secondary_value;
    // Rise Death count zero is a successful no-op and bypasses its cost.
    if (count == 0) {
        return true;
    }
    if (!cost_available()) {
        return false;
    }
    debit_cost();
    for (u32 index = 0; index < count; ++index) {
        if (state.callbacks.create_unit == nullptr) {
            return true;
        }
        const u32 type_id = 0x3bu + roll(2);
        UnitMovementUnit* created = state.callbacks.create_unit(
            state, source, type_id, world_x, world_y);
        // Placement failure terminates the entire creation loop but retains
        // the already-paid cost and any earlier units/effects.
        if (created == nullptr) {
            return true;
        }
        register_effect_unit_ref(state, *created);

        UnitEffectRuntime* spawned = AllocateUnitEffectSlot(state);
        if (spawned == nullptr) {
            // Unit creation comes first.  A full effect pool leaves this unit
            // alive and still consumes all remaining RNG/create iterations.
            continue;
        }
        // Rise Death uses a narrower initializer at
        // 0x004efa40..0x004efa8c.  In particular amount, direction, and the
        // target-coordinate payload remain stale when this slot is recycled.
        spawned->effect_id = 0x4cu;
        spawned->flags = kUnitEffectFlagImpact;
        spawned->tick = 0;
        spawned->frame = 0;
        spawned->source_unit_id = source.id;
        spawned->target_unit_id = created->id;
        spawned->linked_unit_id = created->id;
        spawned->x = created->x;
        spawned->y = created->y;
        append_effect_event(state, UnitEffectEventKind::started, *spawned);
    }
    return true;
}

bool DispatchSelectedUnitActionEffect(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 action_id, UnitMovementUnit& source,
    UnitMovementUnit* target, i32 world_x, i32 world_y) {
    if (action_id >= 0x2e) {
        return false;
    }
    // FUN_004ef7b8 dispatches through the 46-entry initializer table at
    // 0x0086a4b8.  Entries 0x1e/0x1f point at the carry-return stub and never
    // create a runtime effect (0x1f is started through its separate command-
    // attachment path instead).
    if (action_id == 0x1e || action_id == 0x1f) {
        return false;
    }

    const u32 effect_id = action_id + 0x3d;
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect_id);
    if (definition == nullptr) {
        return false;
    }

    const auto scaled_effect_amount = [&]() {
        const u32 variant_bonus_percent =
            CalculateUnitVariantScaledBonus61c(source);
        return definition->damage_amount + static_cast<u32>(
            (static_cast<u64>(definition->damage_amount) *
                variant_bonus_percent) / 100u);
    };

    // Four initializer-table entries perform unit lifecycle mutations before
    // the common effect initializer.  Keeping those mutations here is
    // important: their tick handlers assume that the linked unit has already
    // been created or moved back from the lifecycle list.
    switch (action_id) {
    case 8: {
        // Fake (0x004efd35) creates a temporary clone of the selected target,
        // but gives it the caster's owner and a deliberately reduced runtime
        // stat block.  The effect follows the clone, not the original target.
        if (target == nullptr || state.callbacks.create_unit == nullptr) {
            return false;
        }
        UnitMovementUnit* clone = state.callbacks.create_unit(
            state, source, target->type_id, target->x, target->y);
        if (clone == nullptr) {
            return false;
        }
        clone->runtime_flags |= 0x10u;
        clone->command_flags &= ~0x40u;
        clone->max_secondary_value =
            definition->action_create_unit_secondary_value;
        clone->secondary_value = definition->action_create_unit_secondary_value;
        clone->runtime_stat_1c = 0;
        clone->action_mode = 0;
        clone->type_flags &= 0x231u;
        clone->script_bit_flags = 0;
        clone->command_bits.fill(0);
        clone->max_health = source.health;
        clone->health = source.health;
        clone->runtime_stat_20 = source.runtime_stat_20;
        register_effect_unit_ref(state, *clone);
        target = clone;
        break;
    }
    case 0x0a: {
        // Resurrect accepts only a lifecycle corpse whose raw +0x6c marker is
        // not one.  It restores percentages of the target's own maxima.
        if (!effect_target_is_revivable_corpse(target)) {
            return false;
        }
        return_effect_target_to_idle(*target);
        target->runtime_flags &= ~kUnitActionTargetTransient;
        target->command_flags &= ~0x10u;
        target->runtime_flags |= 1u;
        target->owner_id = source.owner_id;
        // The loader exposes raw +0x1e8 twice: damage_amount is its unsigned
        // magnitude, while action_target_health_delta is negated for the
        // command-damage path.  Resurrection needs the unscaled magnitude.
        const u32 health_percent = definition->damage_amount;
        target->health = static_cast<u32>(
            (static_cast<u64>(target->max_health) * health_percent) / 100u);
        target->secondary_value = static_cast<u32>(
            (static_cast<u64>(target->max_secondary_value) *
                definition->action_create_unit_secondary_value) / 100u);
        reactivate_effect_target(state, *target);
        break;
    }
    case 0x19: {
        // Rebirth revives the existing target with an absolute JW2_11 health
        // value.  It deliberately preserves command flag 0x10 and secondary.
        if (!effect_target_is_revivable_corpse(target)) {
            return false;
        }
        return_effect_target_to_idle(*target);
        target->runtime_flags &= ~kUnitActionTargetTransient;
        target->runtime_flags |= 0x80u;
        target->owner_id = source.owner_id;
        target->health = definition->damage_amount;
        target->pending_command = {};
        target->deferred_command_count = 0;
        reactivate_effect_target(state, *target);
        break;
    }
    case 0x1c: {
        // Bonefighter converts the corpse in-place to unit type 0x5a and
        // rebuilds all mutable definition-derived fields before activation.
        if (!effect_target_is_revivable_corpse(target)) {
            return false;
        }
        const UnitMovementDefinition* bonefighter =
            find_effect_unit_definition(state, 0x5au);
        if (bonefighter == nullptr) {
            return false;
        }
        return_effect_target_to_idle(*target);
        target->runtime_flags &= ~kUnitActionTargetTransient;
        target->command_flags &= ~0x10u;
        target->runtime_flags |= 0x81u;
        target->owner_id = source.owner_id;
        target->type_id = 0x5a;
        target->definition = *bonefighter;
        target->max_health = bonefighter->initial_max_health;
        target->health = bonefighter->initial_max_health;
        target->max_secondary_value =
            bonefighter->initial_max_secondary_value;
        target->secondary_value = (target->max_secondary_value >> 2) +
            (target->max_secondary_value >> 3);
        target->runtime_stat_1c = bonefighter->profile_offense_value;
        target->runtime_stat_20 = bonefighter->profile_defense_value;
        target->runtime_stat_28 = bonefighter->initial_secondary_value;
        target->type_flags = bonefighter->type_flags;
        target->script_bit_flags = bonefighter->initial_script_bit_flags;
        target->string_slot = 0;
        target->cargo_amount = 0;
        target->elite_progress_value = 0;
        target->status_timer = 0;
        target->command_bits.fill(0);
        target->pending_command = {};
        target->deferred_command_count = 0;
        target->direction = 1;
        reactivate_effect_target(state, *target);
        break;
    }
    default:
        break;
    }

    // Shield's initializer (0x004efd09) stores the effect pointer on the
    // target.  Recasting an active shield refreshes that record instead of
    // allocating a second effect (0x004ef7fa..0x004ef86d).
    if (action_id == 2 && target != nullptr &&
        (target->runtime_flags & 0x100u) != 0) {
        if (UnitEffectRuntime* shield =
                effect_slot_from_original_offset_allow_inactive(
                    state, target->linked_effect_slot_offset)) {
            shield->amount = scaled_effect_amount();
            // The caller already reserved this pool slot.  Return it silently;
            // the original refresh path never publishes a second effect.
            effect.active = false;
            ReleaseUnitEffectSlot(state, effect);
            return true;
        }
    }

    // FUN_004efb6f..0x004efb96 writes only amount, frame, tick and id before
    // dispatching through the selected-action initializer table.  Preserve
    // every other payload word from the recycled pool node.
    const u32 retained_slot_direction = effect.direction;
    const u32 retained_slot_flags = effect.flags;
    effect.active = true;
    effect.effect_id = effect_id;
    effect.amount = definition->damage_amount;
    effect.frame = 0;
    effect.tick = 0;
    append_effect_event(state, UnitEffectEventKind::started, effect);
    // FUN_004ef7b8 scales the absolute JW2_11 +0x1e8 effect amount by the
    // source unit's level bonus from JW2_09 +0x324/+0x32c.  Callers that use
    // the allocated effect as a reservation token may still deliberately
    // overwrite amount after this dispatch, matching the original flow.
    effect.amount = scaled_effect_amount();

    const auto initialize_immediate_at_point = [&](UnitMovementUnit* linked,
                                                   i32 x, i32 y) {
        effect.source_unit_id = source.id;
        effect.target_unit_id = linked != nullptr ? linked->id : 0;
        effect.linked_unit_id = effect.target_unit_id;
        effect.x = x;
        effect.y = y;
        effect.flags = kUnitEffectFlagImpact;
        effect.frame = 0;
        effect.tick = 0;
    };

    bool initialized = false;
    switch (action_id) {
    case 8:
        // Fake's common initializer receives the newly created clone.
        initialize_immediate_at_point(target, target->x, target->y);
        initialized = true;
        break;
    case 4:
        // Teleport initializer 0x004efd1e links the effect back to the caster
        // while retaining the commanded destination coordinates.
        initialize_immediate_at_point(&source, world_x, world_y);
        initialized = true;
        break;
    case 0x0a:
        // Resurrect preserves the command's incoming world coordinates.
        initialize_immediate_at_point(target, world_x, world_y);
        initialized = true;
        break;
    case 0x0c:
        // Suicide initializer 0x004efe30 uses the caster for both pointers and
        // anchors the effect at the caster's current position.
        initialize_immediate_at_point(&source, source.x, source.y);
        initialized = true;
        break;
    case 0x10:
    case 0x25:
        // Sky Fallout (0x004f0118) and Blasting (0x004f028a) begin in the
        // active state, not the generic impact state.  Their tick handlers
        // consume frame/tick directly at the commanded point.
        effect.source_unit_id = source.id;
        effect.target_unit_id = target != nullptr ? target->id : 0;
        effect.linked_unit_id = effect.target_unit_id;
        effect.x = world_x;
        effect.y = world_y;
        effect.flags = 0;
        effect.frame = 0;
        effect.tick = 0;
        initialized = true;
        break;
    case 0x19:
    case 0x1c:
        // Rebirth and Bonefighter explicitly replace EDX/EBX with target x/y
        // immediately before entering the common initializer.
        initialize_immediate_at_point(target, target->x, target->y);
        initialized = true;
        break;
    case 0x26:
        // Berry-fly's dedicated initializer 0x004efb9d keeps the incoming
        // EDX/EBX point as effect raw +0x20/+0x24.  The reserved-tile caller
        // at 0x004cb350 passes unit raw +0x6c/+0x70 here; replacing that point
        // with the worker's source center changes both the projectile path and
        // state 0x58's `(effect.x + effect.y) & 7` direction-update cadence.
        if (target != nullptr) {
            // Unlike the generic unit-to-unit initializer, this table entry
            // also seeds the unlimited path sentinel and clears every flag
            // except the Bresenham major-axis bit.
            initialize_reserved_tile_completion_path(
                state, effect, source, *target, world_x, world_y);
            initialized = true;
        }
        break;
    case 0x27:
        // The construction-link initializer at 0x004efce8 temporarily swaps
        // ESI/EDI, asks FUN_004c36de for the target unit's action center, then
        // enters the generic non-projectile initializer with the original
        // source/target links restored.  Consequently the effect is anchored
        // at the new structure's center, not at the builder's muzzle.
        if (target != nullptr) {
            initialize_immediate_at_point(
                target, action_center_x(*target), action_center_y(*target));
            initialized = true;
        }
        break;
    case 0x2c: {
        // Bline's initializer (0x004efc8a) stores source/target centers while
        // leaving flags at mode zero; its tick handler interprets flags as
        // command modes 0/1/2, not as generic impact/projectile flags.
        const i32 retained_target_x = effect.target_x;
        const i32 retained_target_y = effect.target_y;
        CalculateUnitEffectSourceAndTargetCenters(
            state, effect, source, target, world_x, world_y);
        const i32 target_center_x = effect.target_x;
        const i32 target_center_y = effect.target_y;
        // The Bline initializer at 0x004efc8a..0x004efce7 writes only the
        // source/target links, source center at +0x20/+0x24, and target center
        // at +0x30/+0x34.  Raw +0x28/+0x2c, +0x04 and +0x08 remain recycled
        // payload.  Keep both typed aliases of +0x30/+0x34 coherent because
        // rendering reads previous_x/y while save export reads abs_delta_x/y.
        effect.previous_x = target_center_x;
        effect.previous_y = target_center_y;
        effect.abs_delta_x = static_cast<u32>(target_center_x);
        effect.abs_delta_y = static_cast<u32>(target_center_y);
        effect.target_x = retained_target_x;
        effect.target_y = retained_target_y;
        effect.direction = retained_slot_direction;
        effect.linked_unit_id = target != nullptr ? target->id : 0;
        effect.flags = retained_slot_flags;
        effect.frame = 0;
        effect.tick = 0;
        initialized = true;
        break;
    }
    default:
        break;
    }

    if (!initialized) {
        if (target != nullptr) {
            InitializeUnitEffectProjectileOrMeleePath(
                state, effect, source, target);
        }
        else if (selected_action_effect_uses_projectile_path(*definition)) {
            effect.target_x = world_x;
            effect.target_y = world_y;
            InitializeUnitEffectProjectilePath(
                state, effect, source, nullptr, world_x, world_y);
        }
        else {
            // Generic initializer 0x004f02b2 places non-path point effects at
            // EDX/EBX and enters impact immediately.  The previous code sent
            // every null-target action down a projectile path from the caster.
            initialize_immediate_at_point(nullptr, world_x, world_y);
        }
    }

    switch (action_id) {
    case 0:
        if (target != nullptr) {
            effect.x = target->x;
            effect.y = target->y;
        }
        break;
    case 2:
        if (target != nullptr) {
            target->runtime_flags |= 0x100u;
            const std::size_t index = effect_slot_index(state, effect);
            if (index != invalid_effect_slot_index()) {
                const u64 original_offset =
                    (static_cast<u64>(index) + 1u) * 0xa8u;
                if (original_offset <= std::numeric_limits<u32>::max()) {
                    target->linked_effect_slot_offset =
                        static_cast<u32>(original_offset);
                }
            }
        }
        break;
    case 3:
        // Mass Temper initializer 0x004f0106 uses +0x30 as its remaining
        // lifetime.  Leaving it zero makes the first decrement wrap forever.
        effect.abs_delta_x = effect.amount;
        break;
    case 0x0d:
        // Noxious Gas initializer 0x004efe43 seeds the same lifetime word from
        // JW2_11 +0x1f4 rather than from the damage amount.
        effect.abs_delta_x = definition->action_source_stat20_delta;
        break;
    case 0x18:
        // Corrupt initializer 0x004f0141 owns command flag 0x2000 for the
        // channel and uses amount as a zero-based linked-damage accumulator.
        if ((source.command_flags & 0x2000u) != 0) {
            ReleaseUnitEffectSlot(state, effect);
            return false;
        }
        effect.amount = 0;
        source.command_flags |= 0x2000u;
        break;
    case 0x1a:
        // Recharge initializer 0x004f0172 explicitly clears +0x14 after the
        // generic setup.  Rebirth (0x19) retains its scaled amount.
        effect.amount = 0;
        break;
    case 0x19:
    case 0x1c:
        effect.tick = effect_growth_countdown(*target);
        break;
    default:
        break;
    }
    return true;
}

void InitializeUnitEffectProjectilePath(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect,
    UnitMovementUnit& source, UnitMovementUnit* target, i32 world_x, i32 world_y) {
    effect.source_unit_id = source.id;
    effect.target_unit_id = target != nullptr ? target->id : 0;
    effect.x = source.x;
    effect.y = source.y;
    effect.target_x = target != nullptr ? target->x : world_x;
    effect.target_y = target != nullptr ? target->y : world_y;
    effect.delta_x = effect.target_x - effect.x;
    effect.delta_y = effect.target_y - effect.y;
    effect.previous_x = effect.x;
    effect.previous_y = effect.y;
    configure_projectile_step_fields(state, effect);
    effect.range = 0xffffffffu;
    effect.closest_distance = 0xffffffffu;
    effect.tick = 0;
    effect.frame = 0xffffffffu;
    effect.flags &= kUnitEffectFlagProjectileYMajor;
}

void QueueVisibleUnitEffectRenderCommands(UnitEffectRuntimeState& state) {
    for (std::size_t index : state.active_effect_indices) {
        if (index >= state.effect_slots.size()) {
            continue;
        }
        UnitEffectRuntime& effect = state.effect_slots[index];
        if (!effect.active || !effect_inside_viewport(state, effect) ||
            !effect_visible_on_current_grid(state, effect)) {
            continue;
        }
        queue_effect_render_command(state, effect);
    }
}

void RenderUnitEffectOrProjectileRuntime(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (!effect.active) {
        return;
    }
    if (effect.effect_id < 0x3d) {
        RenderUnitEffectRuntimeSprite(state, effect);
        return;
    }
    if (DispatchUnitEffectProjectileTrailRenderer(state, effect, effect.effect_id,
            effect.x - state.viewport_left,
            effect.y + 1 - state.viewport_top)) {
        return;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr || definition->sprite_entry == 0) {
        return;
    }
    const u32 sprite_entry = effect_sprite_entry_for_frame(state, *definition, effect);
    append_effect_event(state, UnitEffectEventKind::render, effect,
        effect.target_unit_id, sprite_entry);
    queue_effect_render_command(state, effect);
}

void ConfigureUnitEffectRenderPalette(UnitEffectRuntimeState& state, bool use_rgb565) {
    if (!use_rgb565) {
        state.render_palette.highlight = 0xcf7f;
        state.render_palette.midtone = 0x76bf;
        state.render_palette.shadow = 0x057f;
    } else {
        state.render_palette.highlight = 0x67bf;
        state.render_palette.midtone = 0x3b5f;
        state.render_palette.shadow = 0x02bf;
    }
    state.impact_line_palette.highlight = 0xf988;
    state.impact_line_palette.midtone = 0xc986;
    state.impact_line_palette.shadow = 0x9186;
}

void TickUnitEffectRuntimeList(UnitEffectRuntimeState& state) {
    const std::vector<std::size_t> active_indices = state.active_effect_indices;

    for (std::size_t index : active_indices) {
        if (index >= state.effect_slots.size()) {
            continue;
        }
        UnitEffectRuntime& effect = state.effect_slots[index];
        if (!effect.active) {
            ReleaseUnitEffectSlot(state, effect);
            continue;
        }

        if (effect.effect_id < 0x3d) {
            TickUnitEffectRuntime(state, effect);
        } else {
            DispatchUnitActionEffectCommand(state, effect, effect.effect_id - 0x3d);
        }

        if (!effect.active) {
            ReleaseUnitEffectSlot(state, effect);
        }
    }
}

void InitializeUnitEffectPathToTarget(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, UnitMovementUnit& source, UnitMovementUnit& target) {
    CalculateUnitEffectSourceAndTargetCenters(state, effect, source, &target);
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const u32 action_range = CalculateUnitActionRangeWithProductionAndEquipmentEffects(
        production_state_or_empty(state), source, target.definition.render_class,
        state.equipment_catalog);
    effect.range = definition != nullptr &&
            (definition->behavior_flags & kUnitEffectBehaviorBoundsImpact) != 0
        ? action_range
        : (action_range >> 1) + 0x10u;
    effect.delta_x = effect.target_x - effect.x;
    effect.delta_y = effect.target_y - effect.y;
    effect.previous_x = effect.x;
    effect.previous_y = effect.y;
    effect.tick = 0;
    configure_projectile_step_fields(state, effect);
    effect.closest_distance = 0xffffffffu;
    effect.flags &= kUnitEffectFlagProjectileYMajor;
}

u32 CalculateUnitEffectSourceAndTargetCenters(UnitEffectRuntimeState&,
    UnitEffectRuntime& effect, const UnitMovementUnit& source,
    const UnitMovementUnit* target, i32 world_x, i32 world_y) {
    effect.source_unit_id = source.id;
    const UnitMovementPoint source_delta = unit_effect_source_offset(source);
    effect.x = source.x + source_delta.x;
    effect.y = source.y + source_delta.y;
    if (target != nullptr) {
        effect.target_unit_id = target->id;
        effect.target_x = action_center_x(*target);
        effect.target_y = action_center_y(*target);
    } else {
        effect.target_unit_id = 0;
        effect.target_x = world_x;
        effect.target_y = world_y;
    }
    effect.direction = LookupUnitEffectDirection(effect);
    return effect.direction;
}

u32 LookupUnitEffectDirection(const UnitEffectRuntime& effect) {
    UnitMovementUnit scratch{};
    scratch.x = effect.x;
    scratch.y = effect.y;
    return CalculateUnitDirectionToPoint(scratch, effect.target_x, effect.target_y);
}

void AdvanceUnitEffectProjectileTowardTarget(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (!effect.active) {
        return;
    }
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    const u32 behavior_flags =
        definition != nullptr ? definition->behavior_flags : 0;

    advance_unit_effect_projectile_step(effect);

    const u32 distance = CalculateApproxUnitDistance(
        effect.x, effect.y, effect.target_x, effect.target_y);
    if ((behavior_flags & kUnitEffectBehaviorSkipImpactCheck) != 0) {
        return;
    }
    if ((behavior_flags & kUnitEffectBehaviorBoundsImpact) == 0) {
        if (distance < effect.closest_distance) {
            effect.closest_distance = distance;
            return;
        }
    } else if ((effect.flags & kUnitEffectFlagProjectileBoundsEntered) == 0) {
        UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
        if (target == nullptr || !point_inside_unit_bounds(*target, effect.x, effect.y)) {
            return;
        }
        effect.closest_distance = CalculateApproxUnitDistance(
            effect.x, effect.y, action_center_x(*target), action_center_y(*target));
        effect.flags |= kUnitEffectFlagProjectileBoundsEntered;
        return;
    } else {
        UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
        if (target == nullptr) {
            return;
        }
        const u32 target_distance = CalculateApproxUnitDistance(
            effect.x, effect.y, action_center_x(*target), action_center_y(*target));
        if (target_distance < effect.closest_distance) {
            effect.closest_distance = target_distance;
            return;
        }
    }

    effect.flags = kUnitEffectFlagImpact;
    effect.range = 0;
    effect.frame = 0;
    effect.x = effect.previous_x;
    effect.y = effect.previous_y;
}

void RetargetUnitEffectProjectilePath(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
    if (target == nullptr) {
        return;
    }

    const i32 new_x = action_center_x(*target);
    const i32 new_y = action_center_y(*target);
    UnitEffectRuntime scratch = effect;
    scratch.target_x = new_x;
    scratch.target_y = new_y;
    const u32 new_direction = lookup_unit_effect_direction(state, scratch);
    if (new_direction == effect.direction) {
        return;
    }

    effect.direction = new_direction;
    const i32 delta_x = new_x - effect.x;
    const i32 delta_y = new_y - effect.y;
    configure_projectile_axis_steps(effect, delta_x, delta_y);
    effect.abs_delta_x = absolute_delta(new_x, effect.x);
    effect.abs_delta_y = absolute_delta(new_y, effect.y);
    effect.accumulator_x = effect.abs_delta_x;
    effect.accumulator_y = effect.abs_delta_y;
    if (effect.abs_delta_x < effect.abs_delta_y) {
        effect.flags |= kUnitEffectFlagProjectileYMajor;
    }
}

void ApplyUnitEffectPointImpactAndSpawnChildren(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        finish_effect(state, effect);
        return;
    }

    const u32 radius = definition->impact_radius;
    const u32 parent_effect_id = effect.effect_id;
    const u32 parent_source_id = effect.source_unit_id;
    const i32 parent_x = effect.x;
    const i32 parent_y = effect.y;
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    std::vector<u32> child_targets;
    const auto apply_to_candidate = [&](UnitMovementUnit& unit) {
        if (!unit_effect_area_candidate_passes_common_gates(
                state, effect, source, unit, *definition) ||
            CheckUnitEffectAlreadyHitTarget(effect, unit.id)) {
            return;
        }
        if (!effect_unit_inside_impact_area(effect, unit, radius)) {
            return;
        }
        const u32 amount =
            unit_effect_point_impact_damage(state, effect, source, unit);
        record_effect_hit_target(effect, unit.id);
        append_effect_event(state, UnitEffectEventKind::impact, effect,
            unit.id, amount);
        if (definition->spawn_impact_child) {
            child_targets.push_back(unit.id);
        }
    };

    for_each_effect_unit_in_active_order(state, apply_to_candidate);

    for (u32 target_id : child_targets) {
        UnitEffectRuntime* child = AllocateUnitEffectSlot(state);
        if (child == nullptr) {
            break;
        }
        child->effect_id = parent_effect_id;
        child->flags = kUnitEffectFlagImpact;
        child->tick = 0;
        child->frame = 0;
        child->amount = 0;
        child->source_unit_id = parent_source_id;
        child->target_unit_id = target_id;
        child->linked_unit_id = target_id;
        child->x = parent_x;
        child->y = parent_y;
    }
}

bool CheckUnitEffectAlreadyHitTarget(const UnitEffectRuntime& effect,
    u32 target_unit_id) {
    return std::find(effect.hit_unit_ids.begin(), effect.hit_unit_ids.end(),
        target_unit_id) != effect.hit_unit_ids.end();
}

UnitEffectRuntime* AllocateUnitEffectSlot(UnitEffectRuntimeState& state) {
    ensure_effect_slot_pool_initialized(state);
    while (!state.free_effect_indices.empty()) {
        const std::size_t index = state.free_effect_indices.front();
        state.free_effect_indices.erase(state.free_effect_indices.begin());
        if (index >= state.effect_slots.size()) {
            continue;
        }

        UnitEffectRuntime& effect = state.effect_slots[index];
        // FUN_004f3490 moves the raw 0xa8-byte node between the free and active
        // lists by updating only +0xa0/+0xa4.  Payload initialization belongs
        // to the selected effect initializer and stale, unwritten words remain
        // observable after reuse.
        effect.active = true;
        remove_effect_slot_index(state.active_effect_indices, index);
        state.active_effect_indices.insert(state.active_effect_indices.begin(), index);
        return &effect;
    }

    return nullptr;
}

void ReleaseUnitEffectSlot(UnitEffectRuntimeState& state, UnitEffectRuntime& effect) {
    const std::size_t index = effect_slot_index(state, effect);
    if (effect.active) {
        append_effect_event(state, UnitEffectEventKind::finished, effect);
    }
    // FUN_004f34db likewise unlinks/relinks only raw +0xa0/+0xa4.  Preserve
    // the payload for the next allocation; `active` represents list ownership
    // in this reconstruction rather than a raw payload word.
    effect.active = false;
    if (index == invalid_effect_slot_index()) {
        return;
    }
    remove_effect_slot_index(state.active_effect_indices, index);
    if (!has_effect_slot_index(state.free_effect_indices, index)) {
        state.free_effect_indices.insert(state.free_effect_indices.begin(), index);
    }
}

bool DispatchUnitEffectProjectileTrailRenderer(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 effect_id,
    i32 captured_screen_x, i32 captured_screen_y) {
    const auto generic_impact_sprite_suppressed = [&]() {
        if ((effect.flags & kUnitEffectFlagImpact) == 0) {
            return false;
        }
        const UnitMovementUnit* target =
            find_effect_unit(state, effect.target_unit_id);
        return target != nullptr && (target->runtime_flags & 0x80u) != 0;
    };
    const auto generic_tail_suppressed = [&]() {
        return generic_impact_sprite_suppressed();
    };
    const auto source_visibility_suppressed = [&]() {
        const UnitMovementUnit* source =
            find_effect_unit(state, effect.source_unit_id);
        if (source == nullptr) {
            return generic_tail_suppressed();
        }
        if ((source->runtime_flags & 0x80u) != 0) {
            return true;
        }
        return unit_visible_for_projectile_effect(state, *source)
            ? generic_tail_suppressed()
            : true;
    };

    switch (effect_id) {
    case 0x46:
        if ((effect.flags & kUnitEffectFlagImpact) == 0) {
            return generic_tail_suppressed();
        }
        if (effect.abs_delta_x != 0) {
            return true;
        }
        if (const UnitEffectDefinition* definition =
                find_effect_definition(state, effect.effect_id)) {
            return draw_projectile_parity_impact_sprite(
                *definition, effect, captured_screen_x, captured_screen_y);
        }
        return true;
    case 0x4b:
        // FUN_004f1ae0 gives startup precedence over the impact bit.  A
        // transient record carrying both flags must not shake or consume the
        // two shared sound-variant rolls.
        if ((effect.flags & kUnitEffectFlagStartup) == 0 &&
            (effect.flags & kUnitEffectFlagImpact) != 0) {
            request_projectile_camera_shake(state, effect);
        }
        return generic_tail_suppressed();
    case 0x56:
        if ((effect.flags & kUnitEffectFlagImpact) == 0) {
            return generic_tail_suppressed();
        }
        return draw_projectile_unit_group_impact_sprite(state, effect, 0x31,
            captured_screen_x, captured_screen_y);
    case 0x59:
        if ((effect.flags & kUnitEffectFlagImpact) == 0) {
            return generic_tail_suppressed();
        }
        return draw_projectile_unit_group_impact_sprite(state, effect, 0x5a,
            captured_screen_x, captured_screen_y);
    case 0x5b:
    case 0x5c:
        return true;
    case 0x5d:
    case 0x5e:
    case 0x5f:
    case 0x60:
    case 0x61:
        return source_visibility_suppressed();
    case 0x62: {
        if ((effect.flags & kUnitEffectFlagImpact) != 0) {
            return generic_tail_suppressed();
        }
        const UnitMovementUnit* source =
            find_effect_unit(state, effect.source_unit_id);
        if (source == nullptr ||
            owner_mask_includes_local_player(state, source->owner_id) ||
            world_point_visible_for_local_player(state, effect.x, effect.y)) {
            return generic_tail_suppressed();
        }
        return true;
    }
    case 0x63:
        if ((effect.flags & (kUnitEffectFlagStartup | kUnitEffectFlagImpact)) != 0) {
            return generic_tail_suppressed();
        }
        if (const UnitEffectDefinition* definition =
                find_effect_definition(state, effect.effect_id)) {
            return draw_projectile_direct_active_sprite(*definition, effect,
                captured_screen_x, captured_screen_y);
        }
        return true;
    case 0x67:
        return effect.source_unit_id != effect.target_unit_id ||
            generic_tail_suppressed();
    case 0x69:
        PrepareUnitEffectProjectileTrailRender(state, effect,
            captured_screen_x, captured_screen_y, effect.flags);
        return true;
    case 0x6a: {
        const UnitMovementUnit* source =
            find_effect_unit(state, effect.source_unit_id);
        if (source == nullptr || unit_visible_for_projectile_effect(state, *source)) {
            return generic_tail_suppressed();
        }
        return true;
    }
    default:
        return generic_tail_suppressed();
    }
}

void PrepareUnitEffectProjectileTrailRender(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 screen_x, i32 screen_y, u32 render_kind) {
    const i32 x0 = effect.previous_x - state.viewport_left;
    const i32 y0 = effect.previous_y - state.viewport_top;
    if (render_kind == 0) {
        DrawUnitEffectWideProjectileTrail(state, effect, x0, y0, screen_x, screen_y);
    } else if (render_kind == 1 || render_kind == kUnitEffectFlagStartup) {
        DrawUnitEffectNarrowProjectileTrail(state, effect, x0, y0, screen_x, screen_y);
    }
}

void DrawUnitEffectWideTrailWithPalette(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1,
    const UnitEffectRenderPalette& palette) {
    // Original 0x004f1f52 starts at the current projectile position (x1/y1)
    // and walks back toward its previous position (x0/y0) in eight-pixel
    // Bresenham steps.  Each visible anchor is independently jittered.  The
    // two random calls are also gameplay-significant because they advance the
    // same global seed later used by sound/effect variants.
    i32 base_x = x1;
    i32 base_y = y1;
    i32 anchor_x = base_x;
    i32 anchor_y = base_y;

    i32 step_x = -8;
    i32 dx = base_x - x0;
    if (dx < 0) {
        dx = -dx;
        step_x = 8;
    }
    i32 step_y = -8;
    i32 dy = base_y - y0;
    if (dy < 0) {
        dy = -dy;
        step_y = 8;
    }

    const bool x_major = dx >= dy;
    const u32 segment_count = static_cast<u32>(x_major ? dx : dy) >> 3;
    i32 error_x = dx;
    i32 error_y = dy;
    GameplaySoundState* sound_state = DefaultFrontendGameplaySoundState();

    for (u32 i = 0; i < segment_count; ++i) {
        // The original consumes the Y roll before the X roll.
        const i32 next_y = base_y +
            (sound_state != nullptr
                ? static_cast<i32>(SelectGameplaySoundVariant(*sound_state, 12))
                : 0) - 6;
        const i32 next_x = base_x +
            (sound_state != nullptr
                ? static_cast<i32>(SelectGameplaySoundVariant(*sound_state, 12))
                : 0) - 6;

        append_trail_segment(state, effect, anchor_x, anchor_y,
            next_x, next_y, 12, palette.highlight);
        if (x_major) {
            append_trail_segment(state, effect, anchor_x, anchor_y + 1,
                next_x, next_y + 1, 12, palette.midtone);
            append_trail_segment(state, effect, anchor_x, anchor_y - 1,
                next_x, next_y - 1, 12, palette.midtone);
            append_trail_segment(state, effect, anchor_x, anchor_y + 2,
                next_x, next_y + 2, 12, palette.shadow);
            append_trail_segment(state, effect, anchor_x, anchor_y - 2,
                next_x, next_y - 2, 12, palette.shadow);
        } else {
            append_trail_segment(state, effect, anchor_x + 1, anchor_y,
                next_x + 1, next_y, 12, palette.midtone);
            append_trail_segment(state, effect, anchor_x - 1, anchor_y,
                next_x - 1, next_y, 12, palette.midtone);
            append_trail_segment(state, effect, anchor_x + 2, anchor_y,
                next_x + 2, next_y, 12, palette.shadow);
            append_trail_segment(state, effect, anchor_x - 2, anchor_y,
                next_x - 2, next_y, 12, palette.shadow);
        }

        anchor_x = next_x;
        anchor_y = next_y;
        if (x_major) {
            base_x += step_x;
            error_y += dy;
            if (error_y >= error_x) {
                error_y -= error_x;
                base_y += step_y;
            }
        } else {
            base_y += step_y;
            error_x += dx;
            if (error_x >= error_y) {
                error_x -= error_y;
                base_x += step_x;
            }
        }
    }
}

void DrawUnitEffectWideProjectileTrail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1) {
    DrawUnitEffectWideTrailWithPalette(
        state, effect, x0, y0, x1, y1, state.render_palette);
}

void DrawUnitEffectWideImpactLineTrail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1) {
    const UnitEffectRenderPalette palette{
        state.impact_line_palette.highlight,
        state.impact_line_palette.midtone,
        state.impact_line_palette.shadow,
    };
    DrawUnitEffectWideTrailWithPalette(
        state, effect, x0, y0, x1, y1, palette);
}

void DrawUnitEffectNarrowProjectileTrail(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, i32 x0, i32 y0, i32 x1, i32 y1) {
    // Original 0x004f2353 is the two-line counterpart of the wide trail.
    i32 base_x = x1;
    i32 base_y = y1;
    i32 anchor_x = base_x;
    i32 anchor_y = base_y;

    i32 step_x = -8;
    i32 dx = base_x - x0;
    if (dx < 0) {
        dx = -dx;
        step_x = 8;
    }
    i32 step_y = -8;
    i32 dy = base_y - y0;
    if (dy < 0) {
        dy = -dy;
        step_y = 8;
    }

    const bool x_major = dx >= dy;
    const u32 segment_count = static_cast<u32>(x_major ? dx : dy) >> 3;
    i32 error_x = dx;
    i32 error_y = dy;
    GameplaySoundState* sound_state = DefaultFrontendGameplaySoundState();

    for (u32 i = 0; i < segment_count; ++i) {
        const i32 next_y = base_y +
            (sound_state != nullptr
                ? static_cast<i32>(SelectGameplaySoundVariant(*sound_state, 10))
                : 0) - 5;
        const i32 next_x = base_x +
            (sound_state != nullptr
                ? static_cast<i32>(SelectGameplaySoundVariant(*sound_state, 10))
                : 0) - 5;

        append_trail_segment(state, effect, anchor_x, anchor_y,
            next_x, next_y, 10, state.render_palette.midtone);
        if (x_major) {
            append_trail_segment(state, effect, anchor_x, anchor_y + 1,
                next_x, next_y + 1, 10, state.render_palette.shadow);
        } else {
            append_trail_segment(state, effect, anchor_x + 1, anchor_y,
                next_x + 1, next_y, 10, state.render_palette.shadow);
        }

        anchor_x = next_x;
        anchor_y = next_y;
        if (x_major) {
            base_x += step_x;
            error_y += dy;
            if (error_y >= error_x) {
                error_y -= error_x;
                base_y += step_y;
            }
        } else {
            base_y += step_y;
            error_x += dx;
            if (error_x >= error_y) {
                error_x -= error_y;
                base_x += step_x;
            }
        }
    }
}

bool InitializeUnitEffectProjectileOrMeleePath(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, UnitMovementUnit& source, UnitMovementUnit* target) {
    if (target == nullptr) {
        effect.source_unit_id = source.id;
        effect.x = action_center_x(source);
        effect.y = action_center_y(source);
        effect.target_x = effect.x;
        effect.target_y = effect.y;
        effect.flags |= kUnitEffectFlagImpact;
        return false;
    }

    const UnitEffectDefinition* definition =
        find_effect_definition(state, effect.effect_id);
    if (definition == nullptr) {
        return false;
    }
    // Original raw +0x1c is both the target pointer and the later linked
    // effect reference.  The reconstruction splits those meanings, so keep
    // both aliases synchronized for every selected target initializer.
    effect.linked_unit_id = target->id;
    if (!selected_action_effect_uses_projectile_path(*definition)) {
        const u32 retained_direction = effect.direction;
        const i32 retained_target_x = effect.target_x;
        const i32 retained_target_y = effect.target_y;
        const i32 retained_previous_x = effect.previous_x;
        const i32 retained_previous_y = effect.previous_y;
        CalculateUnitEffectSourceAndTargetCenters(state, effect, source, target);
        // Generic non-projectile initializer 0x004f02b2 writes neither raw
        // +0x04 nor +0x28..+0x34.  The shared center helper derives direction
        // and target coordinates, so restore every recycled word it touches.
        effect.direction = retained_direction;
        effect.target_x = retained_target_x;
        effect.target_y = retained_target_y;
        effect.previous_x = retained_previous_x;
        effect.previous_y = retained_previous_y;
        effect.flags = kUnitEffectFlagImpact;
        effect.frame = 0;
        effect.tick = 0;
        return true;
    }

    InitializeUnitEffectPathToTarget(state, effect, source, *target);
    return true;
}

u32 UnitEffectCommandDistanceGate(const UnitMovementUnit& source) {
    return source.definition.effect_command_distance_gate;
}

bool selected_unit_auto_effect_gate(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 selector) {
    if (state.callbacks.selected_production_gate != nullptr) {
        return state.callbacks.selected_production_gate(state, source, selector);
    }
    return selector < 32 &&
        (source.definition.support_source_flags & (1u << selector)) != 0;
}

bool selected_unit_auto_effect_source_allows(
    const UnitEffectRuntimeState& state, const UnitMovementUnit& source) {
    if ((source.area_marker_flags & 0x80000000u) != 0) {
        return false;
    }
    if (state.players == nullptr) {
        return true;
    }
    return source.owner_id < state.players->slot_states.size() &&
        state.players->slot_states[source.owner_id] ==
            static_cast<u8>(PlayerSlotState::player_controlled);
}

u32 unit_effect_action_direction_mode(const UnitEffectRuntimeState& state,
    u32 action_id) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, action_id + 0x3du);
    return definition != nullptr ? definition->action_direction_mode : 0;
}

u32 unit_effect_action_target_distance_gate(const UnitEffectRuntimeState& state,
    u32 action_id) {
    const UnitEffectDefinition* definition =
        find_effect_definition(state, action_id + 0x3du);
    return definition != nullptr ? definition->action_area_damage_radius : 0;
}

UnitEffectActionTargetGateResult EvaluateUnitEffectActionTargetGate(
    UnitEffectRuntimeState& state, UnitMovementUnit& source, u32 action_id) {
    const auto compare_point = [&](i32 x, i32 y) {
        UnitEffectActionTargetGateResult result{};
        const u32 distance =
            CalculateApproxUnitDistance(source.x, source.y, x, y);
        const u32 limit = UnitEffectCommandDistanceGate(source);
        result.value = 0;
        result.carry = limit < distance;
        return result;
    };
    if (action_id == 4) {
        return UnitEffectActionTargetGateResult{4, false};
    }
    if (action_id == 0x18) {
        UnitMovementUnit* target = source.target;
        if (target == nullptr ||
            (target->runtime_flags & kUnitActionTargetTransient) != 0) {
            return UnitEffectActionTargetGateResult{1, true};
        }
        const u32 distance = default_action_distance(source, *target);
        const u32 limit = unit_effect_action_target_distance_gate(state, action_id);
        return UnitEffectActionTargetGateResult{0, limit < distance};
    }

    switch (unit_effect_action_direction_mode(state, action_id)) {
    case 0:
        return UnitEffectActionTargetGateResult{action_id, false};
    case 1:
        return compare_point(source.active_command_payload.y,
            static_cast<i32>(source.active_command_payload.value));
    case 2:
        if (source.target != nullptr &&
            (source.target->runtime_flags & kUnitActionTargetTransient) == 0) {
            return compare_point(source.target->x, source.target->y);
        }
        return compare_point(source.active_command_payload.y,
            static_cast<i32>(source.active_command_payload.value));
    case 3:
        if (source.target == nullptr ||
            (source.target->runtime_flags &
                (kUnitActionTargetTransient | kUnitActionTargetClassBlocked)) != 0) {
            return UnitEffectActionTargetGateResult{1, true};
        }
        return compare_point(source.target->x, source.target->y);
    case 4:
        if (source.target == nullptr ||
            (source.target->runtime_flags & kUnitActionTargetTransient) == 0 ||
            source.target->path_target_x == 1) {
            return UnitEffectActionTargetGateResult{1, true};
        }
        return compare_point(source.target->x, source.target->y);
    default:
        return UnitEffectActionTargetGateResult{1, true};
    }
}

u32 CheckUnitEffectActionTargetGate(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 action_id) {
    return EvaluateUnitEffectActionTargetGate(state, source, action_id).value;
}

void ApplyUnitEffectAreaDamageByRenderClassMask(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 amount, u32 radius, u32 render_class_mask) {
    if (radius == 0) {
        UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id);
        if (target != nullptr) {
            append_effect_event(state, UnitEffectEventKind::impact, effect,
                target->id, amount);
        }
        return;
    }

    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (!unit_effect_area_source_allows_scan(source)) {
        return;
    }

    UnitEffectDefinition fallback_definition{};
    const UnitEffectDefinition& definition =
        unit_effect_area_definition_or_fallback(state, effect, fallback_definition);
    for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
        if (!unit_effect_area_candidate_passes_common_gates(
                state, effect, source, unit, definition,
                kUnitEffectAreaRuntimeSkipMask, false)) {
            return;
        }
        const u32 render_class = unit.definition.render_class;
        if (render_class < 32 &&
            (render_class_mask & (1u << render_class)) == 0) {
            return;
        }
        if (!effect_unit_inside_impact_area(effect, unit, radius)) {
            return;
        }
        const u32 scaled_amount =
            effect_unit_area_damage_amount(effect, unit, amount, radius);
        if (scaled_amount == 0) {
            return;
        }
        append_effect_event(state, UnitEffectEventKind::impact, effect,
            unit.id, scaled_amount);
    });
}

void ApplyUnitEffectAreaDamageByOwnerMask(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 amount, u32 radius, u32 blocked_owner_mask) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (radius == 0 || !unit_effect_area_source_allows_scan(source)) {
        return;
    }

    UnitEffectDefinition fallback_definition{};
    const UnitEffectDefinition& definition =
        unit_effect_area_definition_or_fallback(state, effect, fallback_definition);
    for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
        if (unit_owner_bit_set(blocked_owner_mask, unit) ||
            !unit_effect_area_candidate_passes_common_gates(
                state, effect, source, unit, definition,
                kUnitEffectAreaRuntimeSkipMask, false)) {
            return;
        }
        if (!effect_unit_inside_impact_area(effect, unit, radius)) {
            return;
        }
        const u32 scaled_amount =
            effect_unit_area_damage_amount(effect, unit, amount, radius);
        if (scaled_amount == 0) {
            return;
        }
        append_effect_event(state, UnitEffectEventKind::impact, effect,
            unit.id, scaled_amount);
    });
}

void ApplyUnitEffectAreaDamageByUnitFlagMask(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect, u32 amount, u32 radius, u32 unit_flag_mask) {
    UnitMovementUnit* source = find_effect_unit(state, effect.source_unit_id);
    if (radius == 0 || !unit_effect_area_source_allows_scan(source)) {
        return;
    }

    UnitEffectDefinition fallback_definition{};
    const UnitEffectDefinition& definition =
        unit_effect_area_definition_or_fallback(state, effect, fallback_definition);
    for_each_effect_unit_in_active_order(state, [&](UnitMovementUnit& unit) {
        if ((unit.type_flags & unit_flag_mask) == 0 ||
            !unit_effect_area_candidate_passes_common_gates(
                state, effect, source, unit, definition,
                kUnitEffectAreaRuntimeSkipMask, false)) {
            return;
        }
        if (!effect_unit_inside_impact_area(effect, unit, radius)) {
            return;
        }
        const u32 scaled_amount =
            effect_unit_area_damage_amount(effect, unit, amount, radius);
        if (scaled_amount == 0) {
            return;
        }
        append_effect_event(state, UnitEffectEventKind::impact, effect,
            unit.id, scaled_amount);
    });
}

bool AdvanceUnitEffectPathStepAndCheckTargetBounds(UnitEffectRuntimeState& state,
    UnitEffectRuntime& effect) {
    if (!effect.active) {
        return false;
    }
    advance_unit_effect_projectile_step(effect);
    if (UnitMovementUnit* target = find_effect_unit(state, effect.target_unit_id)) {
        if (point_inside_unit_bounds(*target, effect.x, effect.y)) {
            effect.flags = kUnitEffectFlagImpact;
            effect.frame = 0;
            return true;
        }
    }
    return false;
}

bool DispatchSelectedUnitAutoEffectCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, i32 target_x, i32 target_y) {
    if (!selected_unit_auto_effect_source_allows(state, source) ||
        (source.definition.support_source_flags &
            kUnitEffectAutoCommandLowSelectorMask) == 0) {
        return false;
    }
    if (selected_unit_auto_effect_gate(state, source, 1) &&
        IssueSelectedUnitPrimaryTargetEffectCommand(source)) {
        return true;
    }
    if (selected_unit_auto_effect_gate(state, source, 2) &&
        IssueSelectedUnitRepairNearestAllyCommand(state, source,
            UnitEffectCommandDistanceGate(source))) {
        return true;
    }
    if (selected_unit_auto_effect_gate(state, source, 6) &&
        IssueSelectedUnitSecondaryTargetEffectCommand(source)) {
        return true;
    }
    if (selected_unit_auto_effect_gate(state, source, 9)) {
        return false;
    }
    if (selected_unit_auto_effect_gate(state, source, 10) &&
        IssueSelectedUnitHarvestNearestResourceCommand(state, source,
            UnitEffectCommandDistanceGate(source))) {
        return true;
    }
    if (selected_unit_auto_effect_gate(state, source, 11) &&
        IssueSelectedUnitSourceOnlyTargetCommand(source)) {
        return true;
    }
    if (selected_unit_auto_effect_gate(state, source, 15) &&
        IssueSelectedUnitFullHealthTargetCommand(source, source.target,
            target_x, target_y)) {
        return true;
    }
    if (selected_unit_auto_effect_gate(state, source, 0) &&
        IssueSelectedUnitTargetFlagCommandIfAllowed(source, 0x10)) {
        return true;
    }
    return false;
}

bool IssueSelectedUnitPrimaryTargetEffectCommand(UnitMovementUnit& source) {
    UnitMovementUnit* target = source.target;
    return queue_unit_effect_command(source, target,
        target != nullptr ? target->x : 0,
        target != nullptr ? target->y : 0, 1);
}

bool IssueSelectedUnitTargetFlagCommandIfAllowed(UnitMovementUnit& source,
    u32 required_target_flags) {
    if (source.target == nullptr ||
        (source.target->definition.action_effect_flags & required_target_flags) == 0) {
        return false;
    }
    return queue_unit_effect_command(source, source.target,
        source.target->x, source.target->y, 0);
}

bool IssueSelectedUnitRepairNearestAllyCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range) {
    const u32 relation_mask = source_owner_relation_mask(state, source);
    UnitMovementUnit* target = find_first_effect_unit_in_range(state, source, range,
        [relation_mask](const UnitMovementUnit& unit) {
            return unit.type_id < 0x60 &&
                unit_owner_bit_set(relation_mask, unit) &&
                unit.health < unit.max_health &&
                (unit.runtime_flags & kUnitEffectRepairRuntimeSkipMask) == 0;
        }, true);
    if (target == nullptr) {
        return false;
    }
    return queue_unit_effect_command(source, target, source.x, source.y, 2);
}

u32 PassThroughUnitEffectGateValueA(u32 value) {
    return value;
}

u32 PassThroughUnitEffectGateValueB(u32 value) {
    return value;
}

bool IssueSelectedUnitSecondaryTargetEffectCommand(UnitMovementUnit& source) {
    UnitMovementUnit* target = source.target;
    return queue_unit_effect_command(source, target,
        target != nullptr ? target->x : 0,
        target != nullptr ? target->y : 0, 6);
}

u32 PassThroughUnitEffectGateValueC(u32 value) {
    return value;
}

u32 PassThroughUnitEffectGateValueD(u32 value) {
    return value;
}

bool IssueSelectedUnitHarvestNearestResourceCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range) {
    UnitMovementUnit* target = find_first_effect_unit_in_range(state, source, range,
        [](const UnitMovementUnit& unit) {
            return unit.owner_id < 8 && unit.type_id < 0x60 &&
                (unit.definition.action_effect_flags & 0x40) != 0 &&
                unit.path_target_x != 1;
        });
    if (target == nullptr) {
        return false;
    }
    return queue_unit_effect_command(source, target, target->x, target->y, 10);
}

bool IssueSelectedUnitSourceOnlyTargetCommand(UnitMovementUnit& source) {
    if (source.type_id >= 0x60) {
        return false;
    }
    UnitMovementUnit* target = source.target;
    return queue_unit_effect_command(source, target,
        target != nullptr ? target->x : 0,
        target != nullptr ? target->y : 0, 0x0b);
}

bool IssueSelectedUnitNearestHostileCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range) {
    const u32 relation_mask = source_owner_relation_mask(state, source);
    UnitMovementUnit* target = find_first_effect_unit_in_range(state, source, range,
        [relation_mask](const UnitMovementUnit& unit) {
            return !unit_owner_bit_set(relation_mask, unit) && unit.owner_id < 8 &&
                unit.definition.render_class != 2 &&
                unit.definition.render_class != 3;
        });
    if (target == nullptr) {
        return false;
    }
    return queue_unit_effect_command(source, target, source.x, source.y, 0x0e);
}

bool IssueSelectedUnitNearestFlaggedHostileCommand(UnitEffectRuntimeState& state,
    UnitMovementUnit& source, u32 range, u32 required_target_flags) {
    const u32 relation_mask = source_owner_relation_mask(state, source);
    UnitMovementUnit* target = find_first_effect_unit_in_range(state, source, range,
        [relation_mask, required_target_flags](const UnitMovementUnit& unit) {
            return !unit_owner_bit_set(relation_mask, unit) && unit.owner_id < 8 &&
                (unit.definition.action_effect_flags & required_target_flags) != 0;
        });
    if (target == nullptr) {
        return false;
    }
    return queue_unit_effect_command(source, target, source.x, source.y, 0x0e);
}

u32 PassThroughUnitEffectGateValueE(u32 value) {
    return value;
}

bool IssueSelectedUnitFullHealthTargetCommand(UnitMovementUnit& source,
    UnitMovementUnit* target, i32 target_x, i32 target_y) {
    if (source.health < source.max_health) {
        return false;
    }
    return queue_unit_effect_command(source, target, target_x, target_y, 0x0f);
}

u32 PassThroughUnitEffectGateValueF(u32 value) {
    return value;
}

} // namespace ranker
