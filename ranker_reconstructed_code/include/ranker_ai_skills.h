#pragma once

#include "ranker_ai_skill_catalog.h"
#include "ranker_unit_movement.h"

namespace ranker {

// Read-only counterparts of the original production selector and click gates
// (FUN_004db92c and 0x004db238). No resources, effect slots or RNG are mutated.
inline bool AiSkillSourceAvailable(const UnitMovementUnit& source, u32 ability,
    u32 research_level, bool attachment = false) {
    if (ability >= kAiSkillDefinitions.size() || !source.active || !source.health ||
        (source.runtime_flags & 4u) || source.command_entry_lockout_ticks) return false;
    const auto& d = kAiSkillDefinitions[ability];
    if (!d.present || ability == 7 || ability == 30) return false;
    if (ability == 31) return (source.command_flags & 0x800u) != 0;
    if (ability >= 32 || (!(source.script_bit_flags & (1u << ability)) && !attachment)) return false;
    if (ability == 19 && (source.command_flags & 0x800u)) return false;
    if (attachment) return true;
    return (d.research == 0xffffffffu || research_level != 0) &&
        source.status_timer >= d.minimum_status && source.secondary_value >= d.mana_cost &&
        source.health > d.health_cost;
}

inline bool AiSkillTargetAvailable(u32 ability, const UnitMovementUnit* target, bool related) {
    if (ability >= kAiSkillDefinitions.size()) return false;
    const auto& d = kAiSkillDefinitions[ability];
    if (!target) return d.mode < 3;
    if (target->definition.render_class >= 32 ||
        (d.target_classes && !(d.target_classes & (1u << target->definition.render_class)))) return false;
    if (d.mode == 4) {
        if (!(target->runtime_flags & 4u) || target->path_target_x == 1) return false;
    } else if (!target->active || !target->health || (target->runtime_flags & 4u)) return false;
    if (d.mode == 3 && (target->runtime_flags & 0x20000000u)) return false;
    // Selector-specific restrictions are independent of direction/render class.
    switch (ability) {
    case 0: return (target->definition.action_effect_flags & 0x10u) != 0;
    case 8: return (target->definition.action_effect_flags & 0x20u) != 0;
    case 10: case 28:
        return !(target->command_flags & 0x003c0000u) &&
            (target->definition.action_effect_flags & 0x40u);
    case 24: return !(target->runtime_flags & 0x20000000u);
    case 25: return !(target->command_flags & 0x003c0000u) && target->type_id == 0x31u;
    case 27: return target->type_id == 0x31u && related;
    default: return true;
    }
}

inline bool AiSkillStanceAvailable(const UnitMovementUnit& source, u32 stance,
    bool enabled, u32 research_level) {
    if (stance >= 4 || !(source.type_flags & (1u << (0x12 + stance)))) return false;
    const u32 flag = 0x4000u << stance;
    if (!enabled) return (source.command_flags & flag) != 0;
    const auto& d = kAiSkillDefinitions[32 + stance];
    return !(source.command_flags & flag) &&
        (d.research == 0xffffffffu || research_level) && source.action_mode > d.health_cost;
}

} // namespace ranker
