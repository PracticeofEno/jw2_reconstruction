#pragma once

#include "ranker_types.h"

#include <vector>

namespace ranker {

constexpr u32 kUnitStateDead = 0x10000000;
constexpr u32 kUnitRuntimeHiddenOrInactive = 0x80;
constexpr u32 kUnitRuntimeShielded = 0x100;
constexpr u32 kUnitAreaDamageSkipMask = 0x20000086;
constexpr u32 kUnitDamageReactionSkipMask = 0x08020064;
constexpr u32 kUnitEliteVariantThreshold = 0x5f;
constexpr u32 kLocalUnderAttackNotifyIntervalMs = 10000;

struct UnitRecord {
    u32 id = 0;
    u32 type_id = 0;
    u32 owner_id = 0;
    u32 command_state = 0;
    u32 command_flags = 0;
    u32 runtime_flags = 0;
    u32 max_hit_points = 0;
    u32 hit_points = 0;
    u32 max_secondary_value = 0;
    u32 secondary_value = 0;
    u32 experience = 0;
    u32 kill_experience_value = 0;
    u32 interaction_range = 0;
    u32 target_priority = 0xffffffff;
    u32 target_tiebreaker = 0xffffffff;
    u32 render_class = 0;
    i32 x = 0;
    i32 y = 0;
    i32 bounds_left = 0;
    i32 bounds_top = 0;
    i32 bounds_right = 0;
    i32 bounds_bottom = 0;
    i32 interaction_bounds_left = 0;
    i32 interaction_bounds_top = 0;
    i32 interaction_bounds_right = 0;
    i32 interaction_bounds_bottom = 0;
    u32 variant = 0;
    u32 shield_points = 0;
    bool point_target = false;
    bool grants_kill_experience = true;
};

struct UnitDamageContext;

using UnitDamageCallback = void (*)(UnitDamageContext& context, UnitRecord& unit);
using UnitDamagePairCallback = void (*)(UnitDamageContext& context, UnitRecord& source,
    UnitRecord& target);
using UnitVisibilityCallback = bool (*)(UnitDamageContext& context, const UnitRecord& unit);
using UnitDamageAmountCallback = u32 (*)(UnitDamageContext& context, UnitRecord& source,
    UnitRecord& target, u32 radius, u32 distance);
using UnitRelationshipCallback = bool (*)(UnitDamageContext& context,
    const UnitRecord& source, const UnitRecord& target);

struct UnitDamageCallbacks {
    UnitDamageAmountCallback calculate_damage = nullptr;
    UnitRelationshipCallback can_damage = nullptr;
    UnitRelationshipCallback can_award_experience = nullptr;
    UnitVisibilityCallback is_visible_to_local_player = nullptr;
    UnitDamagePairCallback on_unit_damaged = nullptr;
    UnitDamageCallback on_unit_defeated_accounting = nullptr;
    UnitDamagePairCallback on_unit_killed = nullptr;
    UnitDamagePairCallback on_linked_visibility_from_source = nullptr;
    UnitDamagePairCallback on_damage_reaction = nullptr;
    UnitDamageCallback on_shield_broken = nullptr;
    UnitDamageCallback on_local_under_attack = nullptr;
    UnitDamageCallback on_attacker_experience_changed = nullptr;
};

struct UnitDamageContext {
    UnitDamageCallbacks callbacks;
    UnitRecord* attacker = nullptr;
    UnitRecord* direct_target = nullptr;
    std::vector<UnitRecord*> active_units;
    i32 center_x = 0;
    i32 center_y = 0;
    u32 direct_damage = 0;
    u32 splash_radius = 0;
    u32 current_tick_ms = 0;
    u32 last_under_attack_tick_ms = 0;
    u32 local_player_owner_id = 0;
    u32 last_under_attack_x = 0;
    u32 last_under_attack_y = 0;
    u32 allowed_area_target_render_class_mask = 0xffffffffu;
    bool area_damage_allows_related_targets = false;
    bool suppress_same_owner_splash = true;
};

u32 CalculateApproxUnitDistance(i32 x0, i32 y0, i32 x1, i32 y1);
u32 CalculateUnitDamageAmountWithDefinitionModifiers(UnitDamageContext& context,
    UnitRecord& source, UnitRecord& target, u32 base_damage);
void AddUnitHitPointsClamped(UnitRecord& unit, u32 amount);
void AddUnitSecondaryValueClamped(UnitRecord& unit, u32 amount);
bool ApplyUnitDamage(UnitDamageContext& context, UnitRecord& target, u32 damage);
void ApplyAreaDamageFromUnit(UnitDamageContext& context);
void ApplyUnitDamageOrAreaDamage(UnitDamageContext& context);
void NotifyLocalPlayerUnitUnderAttack(UnitDamageContext& context, UnitRecord& target);
void HandleUnitDamageReaction(UnitDamageContext& context, UnitRecord& target);
void ProcessUnitKillExperienceAward(UnitDamageContext& context, UnitRecord& defeated);

}
