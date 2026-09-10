#include "ranker_ai_commander.h"
#include "ranker_ai_skills.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace ranker;
void require(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }

struct Fixture {
    UnitMovementUnit caster, ally, enemy, corpse;
    UnitMovementContext movement;
    PlayerSlotRuntimeState players;
    AiActionPlanInput input;
    AiObservation observation;
    CommanderServices services;
    bool corpse_seen = true;
    u32 research = 1;
    const UnitMovementUnit* find(u32 id) const {
        for (const auto* u : {&caster, &ally, &enemy, &corpse}) if (u->id == id) return u;
        return nullptr;
    }
    static AiObservedUnit observed(const UnitMovementUnit& u) {
        AiObservedUnit result;
        result.id = u.id; result.runtime_slot_index = u.id; result.type_id = u.type_id;
        result.owner_id = u.owner_id; result.controlled = u.owner_id == 1;
        result.visible = true; result.alive = u.active; result.type_flags = u.type_flags;
        result.x = u.x; result.y = u.y; result.health = u.health; result.max_health = u.max_health;
        result.secondary_value = u.secondary_value; result.max_secondary_value = u.max_secondary_value;
        result.render_class = u.definition.render_class; result.command_state = u.command_state;
        result.command_flags = u.command_flags; result.action_mode = u.action_mode;
        return result;
    }
    Fixture(u32 type, u32 ability) {
        for (auto* u : {&caster, &ally, &enemy, &corpse}) {
            u->active = true; u->health = u->max_health = 5000;
            u->secondary_value = u->max_secondary_value = 1000; u->runtime_flags = 1;
            u->owner_id = 1; u->type_flags = 0x20; u->command_state = 1;
            u->x = 320; u->y = 320; u->definition.action_effect_flags = 0x70;
        }
        caster.id = 1; caster.type_id = type; caster.script_bit_flags = 1u << ability; caster.status_timer = 10;
        caster.definition.effect_command_distance_gate = 512;
        ally.id = 2; ally.type_id = 0x31; ally.x = 360;
        enemy = ally; enemy.id = 3; enemy.owner_id = 2; enemy.x = 400;
        const u32 classes = kAiSkillDefinitions[ability].target_classes;
        u32 render = 0; while (classes && !(classes & (1u << render))) ++render;
        ally.definition.render_class = enemy.definition.render_class = render;
        corpse = ally; corpse.id = 4; corpse.active = false; corpse.health = 0;
        corpse.runtime_flags = 4; corpse.command_state = kUnitCommandDead;
        movement.map.width = movement.map.height = 32;
        movement.active_units = {&caster, &ally, &enemy}; movement.lifecycle_units = {&corpse};
        players.owner_relation_masks[1] = 1u << 1;
        input.local_owner = 1; input.players = &players; input.movement = &movement;
        input.unit_visible = [](const UnitMovementUnit&, u32, void*) { return true; };
        input.ability_corpse_visible = [](const UnitMovementUnit&, u32, void* data) { return static_cast<Fixture*>(data)->corpse_seen; };
        input.ability_corpse_visibility_user_data = this;
        input.ability_availability_user_data = this;
        input.ability_available = [](const UnitMovementUnit& source, u32 ability, u32 target, i32, i32, u32 owner, void* data) {
            auto& f = *static_cast<Fixture*>(data);
            const auto* unit = f.find(target);
            AiAbilityAvailability result;
            result.available = AiSkillSourceAvailable(source, ability, f.research) &&
                AiSkillTargetAvailable(ability, unit, unit && unit->owner_id == owner);
            return result;
        };
        observation.local_owner = 1; observation.local_relation_mask = 1u << 1;
        observation.map_width_tiles = observation.map_height_tiles = 32; observation.simulation_frame = 1;
        observation.start_x = observation.start_y = 320; observation.population_used = 180;
        observation.tiles.resize(32 * 32);
        for (auto& t : observation.tiles) t.visible = t.explored = t.passable = t.buildable = true;
        observation.research_order_levels.fill(1);
        services.own_tribe = type < 0x40 ? type / 16 : 3; services.autoscout = false;
        services.skill_source = [&](u32 id) {
            CommanderSkillSource info;
            if (id == caster.id) { info.ability_mask = caster.script_bit_flags; info.status = caster.status_timer; info.range = 512; }
            return info;
        };
        services.skill_target = [&](u32 a, u32 id) { auto* target = find(id); return AiSkillTargetAvailable(a, target, target && target->owner_id == 1); };
        services.validator = [&](const AiSemanticAction& a) {
            return a.kind == AiSemanticActionKind::use_ability && bool(PlanAiSemanticActionV1(input, a));
        };
        refresh();
    }
    void refresh() {
        observation.units = {observed(caster), observed(ally), observed(enemy)};
        services.visible_corpses.clear(); if (corpse_seen) services.visible_corpses.push_back(observed(corpse));
    }
};

int main() {
    u32 bindings = 0;
    for (u32 type = 0; type < kAiInitialSkillMasks.size(); ++type) for (u32 ability = 0; ability < 32; ++ability) {
        if (!(kAiInitialSkillMasks[type] & (1u << ability))) continue;
        Fixture f(type, ability); CommanderState state;
        auto view = BuildCommanderView(state, f.observation, f.services);
        u32 slot = 64;
        while (slot < 96 && (!view.mask[slot] || view.macro_plans[slot][0].ability_id != ability)) ++slot;
        if (slot == 96) std::cerr << "missing binding " << type << '/' << ability << '\n';
        require(slot < 96, "catalog skill did not enter the policy mask");
        const auto cast = view.macro_plans[slot][0];
        const auto before = f.caster.secondary_value;
        CommanderAction action{}; action[0] = u8(slot);
        auto orders = CommanderExecute(state, f.observation, view, &action);
        require(orders.size() == 1 && orders[0].ability_id == ability, "policy skill not published exactly once");
        auto packets = PlanAiSemanticActionV1(f.input, orders[0]);
        require(bool(packets) && packets.packets.size() == 1 && packets.packets[0].subtype == 9 &&
            packets.packets[0].arg0 == ability, "skill wire route differs from the original selector");
        require(f.caster.secondary_value == before, "planning consumed live resources");
        require(state.ability_orders[ability] == 1, "skill order not accounted");
        f.observation.simulation_frame = 9;
        auto pending = BuildCommanderView(state, f.observation, f.services);
        require(CommanderExecute(state, f.observation, pending).empty(), "pending cast overwritten by micro");
        f.caster.command_state = 0x65; f.observation.simulation_frame = 17; f.refresh();
        auto casting = BuildCommanderView(state, f.observation, f.services);
        require(std::none_of(casting.mask.begin() + 64, casting.mask.begin() + 96, [](u8 x) { return x != 0; }), "casting unit recast enabled");
        f.caster.command_state = 1; f.caster.script_bit_flags = 0;
        require(!PlanAiSemanticActionV1(f.input, cast), "unit borrowed another unit's skill");
        ++bindings;
    }
    require(bindings == 29, "original skill catalog coverage changed");
    Fixture f(0x1b, 10);
    AiSemanticAction resurrect; resurrect.kind = AiSemanticActionKind::use_ability;
    resurrect.unit_ids = {1}; resurrect.ability_id = 10; resurrect.target_unit_id = 4;
    require(bool(PlanAiSemanticActionV1(f.input, resurrect)), "visible lifecycle target rejected");
    f.corpse_seen = false;
    require(!PlanAiSemanticActionV1(f.input, resurrect), "invisible corpse accepted");
    f.corpse_seen = true; f.input.ability_corpse_visible = nullptr;
    require(!PlanAiSemanticActionV1(f.input, resurrect), "corpse enabled without opt-in");
    f.caster.secondary_value = 0;
    require(!AiSkillSourceAvailable(f.caster, 10, 1), "unaffordable skill accepted");
    Fixture thunder(0x11, 1); thunder.caster.status_timer = 2;
    require(!AiSkillSourceAvailable(thunder.caster, 1, 1), "required unit level ignored");
    Fixture chief(0x09, 0);
    require(!AiSkillSourceAvailable(chief.caster, 0, 0), "required research ignored");
    Fixture hide(0x0e, 19); hide.caster.command_flags = 0x840; hide.refresh();
    AiSemanticAction off; off.kind = AiSemanticActionKind::use_ability; off.unit_ids = {1}; off.ability_id = 31;
    auto packet = PlanAiSemanticActionV1(hide.input, off);
    require(bool(packet) && packet.packets[0].arg0 == 19 && packet.packets[0].arg1 == 0xffffffffu, "HideOff must use selector 19 with mode -1");
    for (u32 stance = 0; stance < 4; ++stance) {
        auto unit = hide.caster; unit.type_flags = 1u << (18 + stance); unit.action_mode = 10; unit.command_flags = 0;
        require(AiSkillStanceAvailable(unit, stance, true, 1), "racial stance missing");
        require(!AiSkillStanceAvailable(unit, stance, true, 0), "stance skipped research gate");
        unit.command_flags = 0x4000u << stance;
        require(!AiSkillStanceAvailable(unit, stance, true, 1) && AiSkillStanceAvailable(unit, stance, false, 0), "stance toggle state incorrect");
    }
    Fixture morph(0x28, 0);
    morph.caster.script_bit_flags = 0; morph.caster.type_flags = 0x08002011;
    morph.caster.runtime_flags |= 0x40000; morph.caster.definition.morph_type_id = 0x28;
    morph.services.skill_source = [&](u32 id) { CommanderSkillSource info; if (id == 1) info.runtime_flags = morph.caster.runtime_flags; return info; };
    morph.services.validator = [&](const AiSemanticAction& a) { return a.kind == AiSemanticActionKind::morph_exit && bool(PlanAiSemanticActionV1(morph.input, a)); };
    morph.refresh(); CommanderState morph_state;
    auto morph_view = BuildCommanderView(morph_state, morph.observation, morph.services);
    u32 exit_slot = 64; while (exit_slot < 96 && !morph_view.mask[exit_slot]) ++exit_slot;
    require(exit_slot < 96, "morphed capability marker permanently disabled morph exit");
    CommanderAction exit{}; exit[0] = u8(exit_slot);
    auto exit_orders = CommanderExecute(morph_state, morph.observation, morph_view, &exit);
    require(exit_orders.size() == 1 && exit_orders[0].kind == AiSemanticActionKind::morph_exit, "morph exit not emitted");
    std::cout << "29 original skill bindings, policy execution, corpse visibility, HideOff, costs/levels/research and cast protection passed\n";
}
