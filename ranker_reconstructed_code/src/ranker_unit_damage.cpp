#include "ranker_unit_damage.h"

#include <algorithm>

namespace ranker {
namespace {

u32 original_abs_delta(i32 lhs, i32 rhs) {
    u32 delta = static_cast<u32>(lhs) - static_cast<u32>(rhs);
    if ((delta & 0x80000000u) != 0) {
        delta = 0u - delta;
    }
    return delta;
}

u32 original_arithmetic_shift_right_2(u32 value) {
    return (value & 0x80000000u) != 0
        ? ((value >> 2) | 0xc0000000u)
        : (value >> 2);
}

bool is_dead_or_inactive(const UnitRecord& unit) {
    return (unit.command_state & kUnitStateDead) != 0 ||
        (unit.runtime_flags & kUnitRuntimeHiddenOrInactive) != 0;
}

bool is_elite_variant(const UnitRecord& unit) {
    return unit.type_id > kUnitEliteVariantThreshold && unit.variant == 1;
}

bool can_damage(UnitDamageContext& context, UnitRecord& target) {
    if (context.attacker == nullptr) {
        return true;
    }
    if (context.callbacks.can_damage != nullptr) {
        return context.callbacks.can_damage(context, *context.attacker, target);
    }
    if (context.suppress_same_owner_splash && context.attacker != &target &&
        context.attacker->owner_id == target.owner_id) {
        return false;
    }
    return true;
}

bool area_candidate(UnitDamageContext& context, UnitRecord& target) {
    if (is_dead_or_inactive(target)) {
        return false;
    }
    if ((target.runtime_flags & kUnitAreaDamageSkipMask) != 0) {
        return false;
    }
    const bool is_direct_target = context.direct_target == &target;
    if (context.attacker == &target && !is_direct_target) {
        return false;
    }
    if (!is_direct_target && !context.area_damage_allows_related_targets &&
        !can_damage(context, target)) {
        return false;
    }
    if (target.render_class < 32 &&
        (context.allowed_area_target_render_class_mask &
            (1u << target.render_class)) == 0) {
        return false;
    }
    return true;
}

u32 default_area_damage(UnitDamageContext& context, UnitRecord& target, u32 distance) {
    if (context.splash_radius == 0) {
        return context.direct_damage;
    }
    if (target.point_target) {
        return context.direct_damage;
    }
    if (distance >= context.splash_radius) {
        return 0;
    }
    return context.direct_damage -
        static_cast<u32>((static_cast<u64>(distance) * context.direct_damage) /
            context.splash_radius);
}

bool contains_point(const UnitRecord& unit, i32 x, i32 y) {
    return unit.bounds_left <= x && x <= unit.bounds_right &&
        unit.bounds_top <= y && y <= unit.bounds_bottom;
}

} // namespace

u32 CalculateApproxUnitDistance(i32 x0, i32 y0, i32 x1, i32 y1) {
    u32 dx = original_abs_delta(x0, x1);
    u32 dy = original_abs_delta(y0, y1);
    if (dx < dy) {
        std::swap(dx, dy);
    }
    return dx + original_arithmetic_shift_right_2(dy);
}

u32 CalculateUnitDamageAmountWithDefinitionModifiers(UnitDamageContext& context,
    UnitRecord& source, UnitRecord& target, u32 base_damage) {
    if (context.callbacks.calculate_damage != nullptr) {
        return context.callbacks.calculate_damage(context, source, target, 0, 0);
    }
    return base_damage;
}

void AddUnitHitPointsClamped(UnitRecord& unit, u32 amount) {
    unit.hit_points = std::min(unit.hit_points + amount, unit.max_hit_points);
}

void AddUnitSecondaryValueClamped(UnitRecord& unit, u32 amount) {
    unit.secondary_value = std::min(unit.secondary_value + amount,
        unit.max_secondary_value);
}

bool ApplyUnitDamage(UnitDamageContext& context, UnitRecord& target, u32 damage) {
    if (is_dead_or_inactive(target)) {
        return false;
    }
    if ((target.command_state & kUnitStateDead) != 0) {
        return false;
    }

    if ((target.runtime_flags & kUnitRuntimeShielded) != 0) {
        if (target.shield_points > damage) {
            target.shield_points -= damage;
            HandleUnitDamageReaction(context, target);
            NotifyLocalPlayerUnitUnderAttack(context, target);
            return false;
        }
        damage -= target.shield_points;
        target.shield_points = 0;
        target.runtime_flags &= ~kUnitRuntimeShielded;
        if (context.callbacks.on_shield_broken != nullptr) {
            context.callbacks.on_shield_broken(context, target);
        }
    }

    if (is_elite_variant(target)) {
        damage *= 2;
    }

    if (target.hit_points <= damage) {
        const bool area_damage = context.splash_radius != 0;
        target.hit_points = 0;
        if (area_damage) {
            target.command_state |= kUnitStateDead;
        }
        if (context.callbacks.on_unit_defeated_accounting != nullptr) {
            context.callbacks.on_unit_defeated_accounting(context, target);
        }
        ProcessUnitKillExperienceAward(context, target);
        if (context.callbacks.on_unit_killed != nullptr && context.attacker != nullptr) {
            context.callbacks.on_unit_killed(context, *context.attacker, target);
        }
        if (!area_damage) {
            target.command_state |= kUnitStateDead;
            NotifyLocalPlayerUnitUnderAttack(context, target);
        }
        return true;
    }

    target.hit_points -= damage;
    if (context.callbacks.on_unit_damaged != nullptr && context.attacker != nullptr) {
        context.callbacks.on_unit_damaged(context, *context.attacker, target);
    }
    HandleUnitDamageReaction(context, target);
    NotifyLocalPlayerUnitUnderAttack(context, target);
    return false;
}

void ApplyAreaDamageFromUnit(UnitDamageContext& context) {
    if (context.splash_radius == 0) {
        return;
    }

    for (UnitRecord* unit : context.active_units) {
        if (unit == nullptr || !area_candidate(context, *unit)) {
            continue;
        }

        u32 distance = 0;
        if (unit->point_target) {
            if (!contains_point(*unit, context.center_x, context.center_y)) {
                continue;
            }
        }
        else {
            distance = CalculateApproxUnitDistance(context.center_x, context.center_y,
                unit->bounds_left, unit->bounds_top);
            if (distance >= context.splash_radius) {
                continue;
            }
        }

        u32 damage = context.callbacks.calculate_damage != nullptr && context.attacker != nullptr
            ? context.callbacks.calculate_damage(context, *context.attacker, *unit,
                  context.splash_radius, distance)
            : default_area_damage(context, *unit, distance);
        ApplyUnitDamage(context, *unit, damage);
    }
}

void ApplyUnitDamageOrAreaDamage(UnitDamageContext& context) {
    if (context.direct_target == nullptr) {
        ApplyAreaDamageFromUnit(context);
        return;
    }

    if (context.splash_radius != 0) {
        ApplyAreaDamageFromUnit(context);
        if ((context.direct_target->command_state & kUnitStateDead) != 0) {
            return;
        }
    }
    else {
        ApplyUnitDamage(context, *context.direct_target, context.direct_damage);
    }
}

void NotifyLocalPlayerUnitUnderAttack(UnitDamageContext& context, UnitRecord& target) {
    if (context.attacker == nullptr) {
        return;
    }
    if (target.owner_id != context.local_player_owner_id ||
        context.attacker->owner_id == context.local_player_owner_id) {
        return;
    }
    if (context.current_tick_ms - context.last_under_attack_tick_ms <=
        kLocalUnderAttackNotifyIntervalMs - 1) {
        return;
    }

    context.last_under_attack_tick_ms = context.current_tick_ms;
    context.last_under_attack_x = static_cast<u32>(target.x);
    context.last_under_attack_y = static_cast<u32>(target.y);
    if (context.callbacks.is_visible_to_local_player != nullptr &&
        !context.callbacks.is_visible_to_local_player(context, target)) {
        return;
    }

    if (context.callbacks.on_local_under_attack != nullptr) {
        context.callbacks.on_local_under_attack(context, target);
    }
}

void HandleUnitDamageReaction(UnitDamageContext& context, UnitRecord& target) {
    if (context.callbacks.on_linked_visibility_from_source != nullptr &&
        context.attacker != nullptr) {
        context.callbacks.on_linked_visibility_from_source(
            context, *context.attacker, target);
    }
    if ((target.runtime_flags & kUnitDamageReactionSkipMask) != 0) {
        return;
    }
    if (context.callbacks.on_damage_reaction != nullptr && context.attacker != nullptr) {
        context.callbacks.on_damage_reaction(context, *context.attacker, target);
    }
}

void ProcessUnitKillExperienceAward(UnitDamageContext& context, UnitRecord& defeated) {
    if (context.attacker == nullptr || context.attacker == &defeated ||
        !defeated.grants_kill_experience) {
        return;
    }
    if (context.callbacks.can_award_experience != nullptr &&
        !context.callbacks.can_award_experience(context, *context.attacker, defeated)) {
        return;
    }

    context.attacker->experience += defeated.kill_experience_value;
    if (context.callbacks.on_attacker_experience_changed != nullptr) {
        context.callbacks.on_attacker_experience_changed(context, *context.attacker);
    }
}

} // namespace ranker
