#pragma once

#include "ranker_map_effects.h"

#include <algorithm>

namespace ranker {

// Original raw-field contract:
//   Unit +0x2c  = held meat/passive recovery reserve (action_mode)
//   Effect +0x30 = dropped meat amount (repeat_count)
//   Unit +0x4c  = worker resource cargo and is unrelated to meat
struct UnitMeatDropPlan {
    bool valid = false;
    u32 effect_id = 0;
    u32 repeat_count = 0;
};

inline u32 SelectUnitMeatMapEffectId(u32 reserve) {
    u32 effect_id = 1;
    if (reserve > 100) {
        ++effect_id;
    }
    if (reserve > 500) {
        ++effect_id;
    }
    if (reserve > 1000) {
        ++effect_id;
    }
    return effect_id;
}

inline UnitMeatDropPlan PlanUnitMeatDrop(u32 reserve,
    u32 passive_recovery_flags, u32 random_bonus = 0) {
    UnitMeatDropPlan plan{};
    if (reserve == 0) {
        return plan;
    }

    plan.valid = true;
    plan.effect_id = SelectUnitMeatMapEffectId(reserve);
    plan.repeat_count = reserve;
    if ((passive_recovery_flags & 2u) == 0) {
        plan.repeat_count += random_bonus;
    }
    return plan;
}

inline void CommitUnitMeatDrop(UnitMovementUnit& unit,
    MapEffectInstance& effect, const UnitMeatDropPlan& plan) {
    if (!plan.valid) {
        return;
    }
    effect.repeat_count = plan.repeat_count;
    unit.action_mode = 0;
}

inline void AddUnitMeatReserve(UnitMovementUnit& unit, u32 amount) {
    unit.action_mode += amount;
}

inline bool TryConsumeUnitMeatReserveForRecovery(UnitMovementUnit& unit) {
    if (unit.max_health == 0 || unit.health >= unit.max_health ||
        unit.definition.passive_recovery_enabled == 0 ||
        (unit.command_flags & 0x2000u) != 0 || unit.action_mode == 0) {
        return false;
    }

    --unit.action_mode;
    ++unit.health;
    unit.health = std::min(unit.health, unit.max_health);
    return true;
}

} // namespace ranker
