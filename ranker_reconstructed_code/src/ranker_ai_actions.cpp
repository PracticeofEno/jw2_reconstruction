#include "ranker_ai_actions.h"

#include <algorithm>

namespace ranker {
namespace {

constexpr u32 kUnitProductionSubtype = 0x01u;
constexpr u32 kUnitOrderSubtype = 0x02u;
constexpr u32 kRallySubtype = 0x08u;
constexpr u32 kResearchSubtype = 0x0cu;
// Schema v2 wire routes (docs/AI_PLAY_TYRANO_FULL_CAPABILITY_DESIGN.md §1.4).
constexpr u32 kAbilitySubtype = 0x09u;
constexpr u32 kForcedOrderSubtype = 0x0au;
constexpr u32 kStopCommand = 0x00u;
constexpr u32 kMoveCommand = 0x04u;
constexpr u32 kAttackCommand = 0x05u;
constexpr u32 kBuildCommand = 0x06u;
constexpr u32 kHarvestCommand = 0x07u;
constexpr u32 kPatrolCommand = 0x09u;
constexpr u32 kBoardCommand = 0x0au;
constexpr u32 kMergeCommand = 0x0bu;
constexpr u32 kMorphEnterCommand = 0x11u;
constexpr u32 kStanceCommandBase = 0x12u;
constexpr u32 kItemUseCommand = 0x16u;
constexpr u32 kMorphExitCommand = 0x1bu;
constexpr u32 kRallyCommand = 0x1fu;
constexpr u32 kHoldPositionCommand = 0x21u;
constexpr u32 kTransferCommand = 0x23u;
constexpr u32 kUnloadCommand = 0x24u;
constexpr u32 kReturnCargoTarget = 0x80000000u;
constexpr u32 kStanceFlagBase = 0x4000u;
// Morph gates (StartUnitMorphEnterIfAvailable / ...ExitIfFlagged).
constexpr u32 kMorphCapabilityFlag = 0x20000u;
constexpr u32 kMorphActiveRuntimeFlag = 0x40000u;
constexpr u32 kMorphedTypeFlag = 0x08000000u;
// Transport gates (unit_can_carry / unit_can_be_boarded / sender case 0x0a).
constexpr u32 kCarrierTypeFlag = 0x400u;
constexpr u32 kBoardablePairEffectFlag = 0x4u;
constexpr u32 kBoardableTransportFlag = 0x4u;
// Worker carrying-cargo marker (sender case 0x1d return-cargo gate).
constexpr u32 kCargoCommandFlag = 0x4u;
// The original UI dispatcher hard-codes the mutant triad recipe
// (ranker_winmain.cpp case 0x0b): 딜로포스 + 프테라스 + 트리세스 -> 뮤턴트.
constexpr u32 kMutantTriadMemberA = 0x24u;
constexpr u32 kMutantTriadMemberB = 0x27u;
constexpr u32 kMutantTriadMemberC = 0x28u;
constexpr u32 kUsableItemId = 0x1bu;
constexpr u32 kQueuedCommandFlag = 0x80000000u;
constexpr u32 kCommandStateMask = 0x00ffffffu;
constexpr u32 kFirstBuildingType = 0x60u;
constexpr u32 kBuildingTypeLimit = 0xaau;
constexpr u32 kMobileUnitTypeLimit = 0x60u;
constexpr u32 kResearchOrderLimit = 0x40u;
constexpr u32 kResearchQueueLimit = 10u;

const UnitMovementUnit* find_active_unit(const UnitMovementContext& movement,
    u32 id) {
    const auto found = std::find_if(movement.active_units.begin(),
        movement.active_units.end(), [id](const UnitMovementUnit* unit) {
            return unit != nullptr && unit->id == id;
        });
    return found == movement.active_units.end() ? nullptr : *found;
}

bool supports_action(const UnitMovementUnit& unit, u32 command) {
    return command < 32u && (unit.type_flags & (1u << command)) != 0;
}

bool point_inside_map(const UnitMovementMap& map, i32 x, i32 y) {
    if (x < 0 || y < 0 || map.width == 0 || map.height == 0) {
        return false;
    }
    const u64 width_pixels = static_cast<u64>(map.width) * 32u;
    const u64 height_pixels = static_cast<u64>(map.height) * 32u;
    return static_cast<u64>(x) < width_pixels &&
        static_cast<u64>(y) < height_pixels;
}

const UnitMovementCell* map_cell_at_world_point(
    const UnitMovementMap& map, i32 x, i32 y, std::size_t& compact_index) {
    if (!point_inside_map(map, x, y)) {
        return nullptr;
    }
    const u32 tile_x = static_cast<u32>(x) >> 5;
    const u32 tile_y = static_cast<u32>(y) >> 5;
    const u32 stride = map.stride_tiles != 0 ? map.stride_tiles : map.width;
    if (stride < map.width) {
        return nullptr;
    }
    const std::size_t map_index =
        static_cast<std::size_t>(tile_y) * stride + tile_x;
    if (map_index >= map.cells.size()) {
        return nullptr;
    }
    compact_index =
        static_cast<std::size_t>(tile_y) * map.width + tile_x;
    return &map.cells[map_index];
}

bool is_visible_target(const AiActionPlanInput& input,
    const UnitMovementUnit& target) {
    if (target.owner_id == input.local_owner) {
        return true;
    }
    return input.unit_visible != nullptr && input.unit_visible(
        target, input.local_owner, input.unit_visibility_user_data);
}

bool is_friendly_target(const AiActionPlanInput& input,
    const UnitMovementUnit& target) {
    if (target.owner_id == input.local_owner) {
        return true;
    }
    if (target.owner_id >= 32u) {
        return false;
    }
    return (input.players->owner_relation_masks[input.local_owner] &
        (1u << target.owner_id)) != 0;
}

GameplayPublishedAction make_packet(u32 owner, u32 subtype,
    const UnitMovementUnit& unit, u32 arg0, u32 arg1 = 0, u32 arg2 = 0,
    u32 arg3 = 0) {
    GameplayPublishedAction packet{};
    packet.subtype = subtype;
    packet.player = owner;
    packet.packed_opcode = (subtype << 24) | (owner & 0xffu);
    packet.unit_offset = unit.id;
    packet.arg0 = arg0;
    packet.arg1 = arg1;
    packet.arg2 = arg2;
    packet.arg3 = arg3;
    return packet;
}

GameplayPublishedAction make_unit_order(u32 owner, u32 command,
    const UnitMovementUnit& unit, u32 target_id, i32 x, i32 y, bool queued) {
    return make_packet(owner, kUnitOrderSubtype, unit,
        command | (queued ? kQueuedCommandFlag : 0u), target_id,
        static_cast<u32>(x), static_cast<u32>(y));
}

bool action_uses_production_id(AiSemanticActionKind kind) {
    return kind == AiSemanticActionKind::produce_unit ||
        kind == AiSemanticActionKind::research ||
        kind == AiSemanticActionKind::build;
}

bool latest_command_is_cancellable(const UnitMovementUnit& unit) {
    if (unit.deferred_command_count > unit.deferred_commands.size()) {
        return false;
    }
    if (unit.deferred_command_count != 0) {
        const u32 state = unit.deferred_commands[
            unit.deferred_command_count - 1].state & kCommandStateMask;
        return state == 0x10u || state == 0x17u || state == 0x22u;
    }

    switch (unit.command_state & kCommandStateMask) {
    case 0x4du:
    case 0x4eu:
    case 0x50u:
    case 0x51u:
    case 0x82u:
    case 0x83u:
        return true;
    default:
        return false;
    }
}

AiProductionAvailability check_production(
    const AiActionPlanInput& input, AiProductionRequestKind kind,
    const UnitMovementUnit& source, u32 production_id, i32 world_x,
    i32 world_y) {
    if (input.production_available == nullptr) {
        return {};
    }
    return input.production_available(kind, source, production_id, world_x,
        world_y, input.local_owner,
        input.production_availability_user_data);
}

AiActionPlanResult reject(AiActionPlanCode code) {
    AiActionPlanResult result{};
    result.code = code;
    return result;
}

} // namespace

AiActionPlanResult PlanAiSemanticActionV1(const AiActionPlanInput& input,
    const AiSemanticAction& action) {
    if (action.schema_version != kAiActionSchemaVersion) {
        return reject(AiActionPlanCode::unsupported_schema);
    }
    if (input.local_owner >= kPlayerSlotCount) {
        return reject(AiActionPlanCode::invalid_local_owner);
    }
    if (action.kind != AiSemanticActionKind::use_ability &&
        action.ability_id != kAiNoAbilityId) {
        return reject(AiActionPlanCode::unexpected_ability);
    }
    if (action.kind != AiSemanticActionKind::set_stance &&
        action.stance_id != kAiNoStanceId) {
        return reject(AiActionPlanCode::invalid_stance);
    }
    if (action.kind == AiSemanticActionKind::no_op) {
        if (!action.unit_ids.empty() || action.target_unit_id != 0 ||
            action.production_id != kAiNoProductionId ||
            action.queue_index != kAiLatestQueueIndex || action.queued) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        AiActionPlanResult result{};
        result.code = AiActionPlanCode::okay;
        return result;
    }
    if (input.players == nullptr) {
        return reject(AiActionPlanCode::missing_players);
    }
    if (input.movement == nullptr) {
        return reject(AiActionPlanCode::missing_movement);
    }
    if (input.movement->map.width == 0 || input.movement->map.height == 0) {
        return reject(AiActionPlanCode::invalid_map_dimensions);
    }
    if (action.unit_ids.empty()) {
        return reject(AiActionPlanCode::empty_unit_selection);
    }
    if (action.unit_ids.size() > kAiMaximumUnitsPerAction) {
        return reject(AiActionPlanCode::too_many_units);
    }
    if (!action_uses_production_id(action.kind) &&
        action.production_id != kAiNoProductionId) {
        return reject(AiActionPlanCode::unexpected_production);
    }
    if (action.kind != AiSemanticActionKind::cancel_production &&
        action.queue_index != kAiLatestQueueIndex) {
        return reject(AiActionPlanCode::unsupported_queue_index);
    }

    std::vector<u32> sorted_ids = action.unit_ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    if (std::adjacent_find(sorted_ids.begin(), sorted_ids.end()) !=
        sorted_ids.end()) {
        return reject(AiActionPlanCode::duplicate_unit_id);
    }

    std::vector<const UnitMovementUnit*> units;
    units.reserve(sorted_ids.size());
    for (u32 id : sorted_ids) {
        const UnitMovementUnit* unit = find_active_unit(*input.movement, id);
        if (unit == nullptr) {
            return reject(AiActionPlanCode::unknown_unit_id);
        }
        if (unit->owner_id != input.local_owner) {
            return reject(AiActionPlanCode::unit_not_owned);
        }
        if (!unit->active || (unit->command_state & kUnitCommandDead) != 0) {
            return reject(AiActionPlanCode::unit_inactive);
        }
        units.push_back(unit);
    }

    if (action.kind == AiSemanticActionKind::produce_unit ||
        action.kind == AiSemanticActionKind::research ||
        action.kind == AiSemanticActionKind::build ||
        action.kind == AiSemanticActionKind::cancel_production ||
        action.kind == AiSemanticActionKind::use_item) {
        if (units.size() != 1) {
            return reject(AiActionPlanCode::requires_single_unit);
        }
    }

    const UnitMovementUnit& primary = *units.front();
    if (action.kind == AiSemanticActionKind::produce_unit) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        if (action.production_id >= kMobileUnitTypeLimit) {
            return reject(AiActionPlanCode::invalid_production_id);
        }
        if (primary.deferred_command_count >= kUnitProductionQueueLimit) {
            return reject(AiActionPlanCode::production_queue_full);
        }
        if (input.production_available == nullptr) {
            return reject(AiActionPlanCode::missing_production_validator);
        }
        const AiProductionAvailability availability = check_production(input,
            AiProductionRequestKind::unit, primary, action.production_id, 0, 0);
        if (!availability.available) {
            return reject(AiActionPlanCode::production_unavailable);
        }

        AiActionPlanResult result{};
        result.code = AiActionPlanCode::okay;
        result.packets.push_back(make_packet(input.local_owner,
            kUnitProductionSubtype, primary, action.production_id));
        return result;
    }

    if (action.kind == AiSemanticActionKind::research) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        if (action.production_id >= kResearchOrderLimit) {
            return reject(AiActionPlanCode::invalid_production_id);
        }
        if (primary.deferred_command_count >= kResearchQueueLimit) {
            return reject(AiActionPlanCode::production_queue_full);
        }
        if (input.production_available == nullptr) {
            return reject(AiActionPlanCode::missing_production_validator);
        }
        const AiProductionAvailability availability = check_production(input,
            AiProductionRequestKind::research, primary, action.production_id,
            0, 0);
        if (!availability.available) {
            return reject(AiActionPlanCode::production_unavailable);
        }

        AiActionPlanResult result{};
        result.code = AiActionPlanCode::okay;
        result.packets.push_back(make_packet(input.local_owner,
            kResearchSubtype, primary, action.production_id,
            availability.secondary_cost));
        return result;
    }

    if (action.kind == AiSemanticActionKind::build) {
        if (action.target_unit_id != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (action.production_id < kFirstBuildingType ||
            action.production_id >= kBuildingTypeLimit) {
            return reject(AiActionPlanCode::invalid_production_id);
        }
        if (!point_inside_map(input.movement->map,
                action.target_x, action.target_y)) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        const i32 aligned_x = action.target_x & ~0x1f;
        const i32 aligned_y = action.target_y & ~0x1f;
        if (input.production_available == nullptr) {
            return reject(AiActionPlanCode::missing_production_validator);
        }
        const AiProductionAvailability availability = check_production(input,
            AiProductionRequestKind::building, primary, action.production_id,
            aligned_x, aligned_y);
        if (!availability.available) {
            return reject(AiActionPlanCode::production_unavailable);
        }

        AiActionPlanResult result{};
        result.code = AiActionPlanCode::okay;
        result.packets.push_back(make_unit_order(input.local_owner,
            kBuildCommand, primary,
            action.production_id - kFirstBuildingType,
            aligned_x, aligned_y, action.queued));
        return result;
    }

    if (action.kind == AiSemanticActionKind::cancel_production) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        if (action.queue_index != kAiLatestQueueIndex) {
            return reject(AiActionPlanCode::unsupported_queue_index);
        }
        if (!latest_command_is_cancellable(primary)) {
            return reject(AiActionPlanCode::nothing_to_cancel);
        }

        // gameplay_1.ply frames 7450..7549 use this exact subtype-01 latest
        // form. The receiver resolves the live queue tail and reroutes
        // research/production-cost entries to their matching refund handler.
        AiActionPlanResult result{};
        result.code = AiActionPlanCode::okay;
        result.packets.push_back(make_packet(input.local_owner,
            kUnitProductionSubtype, primary, 0, 1, kAiLatestQueueIndex, 0));
        return result;
    }

    // ---- Schema v2 kinds ------------------------------------------------

    if (action.kind == AiSemanticActionKind::stop ||
        action.kind == AiSemanticActionKind::hold_position) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            if (action.kind == AiSemanticActionKind::stop) {
                // Wire command 0x00 dispatches straight to the return-idle
                // entry without a capability gate (DispatchUnitCommandStateEntry).
                result.packets.push_back(make_unit_order(input.local_owner,
                    kStopCommand, *unit, 0, 0, 0, false));
            } else {
                // Human hold publishes subtype 0x0a with forced order 0x21
                // (ranker_winmain.cpp case 0x21).
                result.packets.push_back(make_packet(input.local_owner,
                    kForcedOrderSubtype, *unit, kHoldPositionCommand, 0,
                    static_cast<u32>(unit->x), static_cast<u32>(unit->y)));
            }
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::patrol) {
        if (action.target_unit_id != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (!point_inside_map(input.movement->map, action.target_x,
                action.target_y)) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            if (!supports_action(*unit, kPatrolCommand)) {
                return reject(AiActionPlanCode::unit_action_unsupported);
            }
            result.packets.push_back(make_unit_order(input.local_owner,
                kPatrolCommand, *unit, 0, action.target_x, action.target_y,
                action.queued));
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::use_ability) {
        if (action.ability_id >= kAiAbilityIdLimit) {
            return reject(AiActionPlanCode::invalid_ability_id);
        }
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        if (input.ability_available == nullptr) {
            return reject(AiActionPlanCode::missing_ability_validator);
        }
        const UnitMovementUnit* ability_target = nullptr;
        i32 cast_x = action.target_x;
        i32 cast_y = action.target_y;
        if (action.target_unit_id != 0) {
            ability_target = find_active_unit(*input.movement,
                action.target_unit_id);
            if (ability_target == nullptr || !ability_target->active ||
                (ability_target->command_state & kUnitCommandDead) != 0) {
                return reject(AiActionPlanCode::target_inactive);
            }
            // Friendly targets stay legal (heal/buff class effects); only
            // fog-hidden targets are rejected.
            if (!is_visible_target(input, *ability_target)) {
                return reject(AiActionPlanCode::target_not_visible);
            }
            cast_x = ability_target->x;
            cast_y = ability_target->y;
        }
        if (!point_inside_map(input.movement->map, cast_x, cast_y)) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            const AiAbilityAvailability availability = input.ability_available(
                *unit, action.ability_id, action.target_unit_id, cast_x,
                cast_y, input.local_owner,
                input.ability_availability_user_data);
            if (!availability.available) {
                return reject(AiActionPlanCode::ability_unavailable);
            }
            // Subtype 0x09's command byte IS the ability id; the deferred
            // entry prefix routes it into special-ability state 0x64.
            result.packets.push_back(make_packet(input.local_owner,
                kAbilitySubtype, *unit, action.ability_id,
                action.target_unit_id, static_cast<u32>(cast_x),
                static_cast<u32>(cast_y)));
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::morph_enter ||
        action.kind == AiSemanticActionKind::morph_exit) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        const bool enter = action.kind == AiSemanticActionKind::morph_enter;
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            if (enter) {
                // StartUnitMorphEnterIfAvailable gate: a morph target in the
                // definition, capability bit 0x20000, and not already morphed.
                if (unit->definition.morph_type_id == 0 ||
                    (unit->type_flags & kMorphCapabilityFlag) == 0 ||
                    (unit->runtime_flags & kMorphActiveRuntimeFlag) != 0) {
                    return reject(AiActionPlanCode::morph_unavailable);
                }
            } else {
                // StartUnitMorphExitIfFlagged gate: post-morph marker bit.
                if ((unit->type_flags & kMorphedTypeFlag) == 0 ||
                    (unit->runtime_flags & kMorphActiveRuntimeFlag) == 0) {
                    return reject(AiActionPlanCode::not_morphed);
                }
            }
            result.packets.push_back(make_unit_order(input.local_owner,
                enter ? kMorphEnterCommand : kMorphExitCommand, *unit, 0,
                unit->x, unit->y, action.queued));
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::merge_units) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        for (const UnitMovementUnit* unit : units) {
            if (!supports_action(*unit, kMergeCommand)) {
                return reject(AiActionPlanCode::unit_action_unsupported);
            }
        }
        AiActionPlanResult result{};
        if (units.size() == 2) {
            const UnitMovementUnit& first = *units[0];
            const UnitMovementUnit& second = *units[1];
            if (first.type_id != second.type_id) {
                return reject(AiActionPlanCode::merge_recipe_invalid);
            }
            if (first.definition.linked_release_type_id == 0) {
                return reject(AiActionPlanCode::merge_recipe_invalid);
            }
            // The human pair form is two mirrored 0x0b orders
            // (ranker_winmain.cpp:7216-7223).
            const u32 anchor_x = static_cast<u32>(first.x);
            result.packets.push_back(make_unit_order(input.local_owner,
                kMergeCommand, first, second.id,
                static_cast<i32>(anchor_x), 0, action.queued));
            result.packets.push_back(make_unit_order(input.local_owner,
                kMergeCommand, second, first.id,
                static_cast<i32>(anchor_x), 0, action.queued));
        } else if (units.size() == 3) {
            // Mutant triad: the original dispatcher hard-codes the recipe and
            // publishes a three-packet ring (ranker_winmain.cpp:7139-7212).
            const UnitMovementUnit* outer = nullptr;
            const UnitMovementUnit* member_b = nullptr;
            const UnitMovementUnit* member_c = nullptr;
            for (const UnitMovementUnit* unit : units) {
                if (unit->type_id == kMutantTriadMemberA && outer == nullptr) {
                    outer = unit;
                } else if (unit->type_id == kMutantTriadMemberB &&
                    member_b == nullptr) {
                    member_b = unit;
                } else if (unit->type_id == kMutantTriadMemberC &&
                    member_c == nullptr) {
                    member_c = unit;
                }
            }
            if (outer == nullptr || member_b == nullptr ||
                member_c == nullptr) {
                return reject(AiActionPlanCode::merge_recipe_invalid);
            }
            const u32 anchor_x = static_cast<u32>(outer->x);
            result.packets.push_back(make_unit_order(input.local_owner,
                kMergeCommand, *member_c, outer->id,
                static_cast<i32>(anchor_x), 0, action.queued));
            result.packets.push_back(make_unit_order(input.local_owner,
                kMergeCommand, *member_b, member_c->id,
                static_cast<i32>(anchor_x), 0, action.queued));
            result.packets.push_back(make_unit_order(input.local_owner,
                kMergeCommand, *outer, member_b->id,
                static_cast<i32>(anchor_x), 0, action.queued));
        } else {
            return reject(AiActionPlanCode::merge_arity_invalid);
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::board_transport) {
        if (action.target_unit_id == 0) {
            return reject(AiActionPlanCode::missing_target);
        }
        if (action.target_x != 0 || action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        const UnitMovementUnit* carrier = find_active_unit(*input.movement,
            action.target_unit_id);
        if (carrier == nullptr || !carrier->active ||
            (carrier->command_state & kUnitCommandDead) != 0) {
            return reject(AiActionPlanCode::target_inactive);
        }
        if (carrier->owner_id != input.local_owner ||
            (carrier->type_flags & kCarrierTypeFlag) == 0) {
            return reject(AiActionPlanCode::target_not_carrier);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            if (unit->id == carrier->id ||
                (unit->type_flags & kCarrierTypeFlag) != 0) {
                return reject(AiActionPlanCode::passenger_cannot_board);
            }
            // Sender case 0x0a accepts either direction of the capability /
            // action_effect_flags-bit-4 pair; boarding additionally requires
            // the passenger's transport bit (unit_can_be_boarded).
            const bool pairable =
                (supports_action(*unit, kBoardCommand) &&
                    (carrier->definition.action_effect_flags &
                        kBoardablePairEffectFlag) != 0) ||
                ((unit->definition.action_effect_flags &
                     kBoardablePairEffectFlag) != 0 &&
                    supports_action(*carrier, kBoardCommand));
            if (!pairable ||
                (unit->definition.transport_flags &
                    kBoardableTransportFlag) == 0) {
                return reject(AiActionPlanCode::passenger_cannot_board);
            }
            result.packets.push_back(make_unit_order(input.local_owner,
                kBoardCommand, *unit, carrier->id, carrier->x, carrier->y,
                action.queued));
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::unload_transport) {
        if (action.target_unit_id != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (!point_inside_map(input.movement->map, action.target_x,
                action.target_y)) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            // Sender case 0x24 gates unload on the carrier capability bit.
            if (!supports_action(*unit, kBoardCommand)) {
                return reject(AiActionPlanCode::target_not_carrier);
            }
            result.packets.push_back(make_unit_order(input.local_owner,
                kUnloadCommand, *unit, 0, action.target_x, action.target_y,
                action.queued));
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::transfer_secondary) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        // Mirror the balance aggregation (ranker_winmain.cpp case 0x23):
        // average the mobile group's action_mode, then pair each donor at or
        // above the threshold with recipients below it.
        u32 aggregate_count = 0;
        u32 aggregate_sum = 0;
        for (const UnitMovementUnit* unit : units) {
            if (unit->type_id >= kMobileUnitTypeLimit ||
                (unit->runtime_flags & 1u) == 0) {
                continue;
            }
            ++aggregate_count;
            aggregate_sum += unit->action_mode;
        }
        if (aggregate_count == 0) {
            return reject(AiActionPlanCode::nothing_to_transfer);
        }
        const u32 threshold = aggregate_sum / aggregate_count;
        AiActionPlanResult result{};
        std::vector<u32> paired;
        paired.reserve(units.size());
        const auto was_paired = [&paired](u32 unit_id) {
            return std::find(paired.begin(), paired.end(), unit_id) !=
                paired.end();
        };
        for (const UnitMovementUnit* donor : units) {
            if (was_paired(donor->id) ||
                !supports_action(*donor, 0x01u) ||
                donor->action_mode < threshold) {
                continue;
            }
            u32 donor_remaining = donor->action_mode;
            for (const UnitMovementUnit* recipient : units) {
                if (recipient == donor || was_paired(recipient->id)) {
                    continue;
                }
                if (!supports_action(*recipient, 0x01u) ||
                    recipient->action_mode >= threshold) {
                    continue;
                }
                paired.push_back(recipient->id);
                result.packets.push_back(make_unit_order(input.local_owner,
                    kTransferCommand, *recipient, donor->id,
                    static_cast<i32>(threshold), 0, action.queued));
                donor_remaining -= threshold - recipient->action_mode;
                if (donor_remaining <= threshold) {
                    break;
                }
            }
        }
        if (result.packets.empty()) {
            return reject(AiActionPlanCode::nothing_to_transfer);
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::set_stance) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        if (action.stance_id >= kAiStanceCount) {
            return reject(AiActionPlanCode::invalid_stance);
        }
        const u32 command = kStanceCommandBase + action.stance_id;
        const u32 flag = kStanceFlagBase << action.stance_id;
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            if (!supports_action(*unit, command)) {
                continue;
            }
            if (action.stance_on) {
                // Sender cases 0x12-0x15: needs a live secondary budget.
                if (unit->action_mode == 0) {
                    continue;
                }
                result.packets.push_back(make_unit_order(input.local_owner,
                    command, *unit, 0, unit->x, unit->y, action.queued));
            } else {
                // Sender cases 0x26-0x29: cancel form carries arg1=1 and the
                // stance flag mask, and requires the flag to be active.
                if ((unit->command_flags & flag) == 0) {
                    continue;
                }
                result.packets.push_back(make_unit_order(input.local_owner,
                    command, *unit, 1, static_cast<i32>(flag), 0,
                    action.queued));
            }
        }
        if (result.packets.empty()) {
            return reject(action.stance_on ?
                AiActionPlanCode::stance_unavailable :
                AiActionPlanCode::stance_inactive);
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::return_cargo) {
        if (action.target_unit_id != 0 || action.target_x != 0 ||
            action.target_y != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            // Sender case 0x1d: harvest-capable unit currently carrying
            // cargo returns via harvest with the synthetic 0x80000000 target.
            if (!supports_action(*unit, kHarvestCommand) ||
                (unit->command_flags & kCargoCommandFlag) == 0) {
                continue;
            }
            result.packets.push_back(make_unit_order(input.local_owner,
                kHarvestCommand, *unit, kReturnCargoTarget, unit->x, unit->y,
                action.queued));
        }
        if (result.packets.empty()) {
            return reject(AiActionPlanCode::nothing_to_return);
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    if (action.kind == AiSemanticActionKind::use_item) {
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        bool has_item = false;
        for (const u32 slot : primary.item_slots) {
            if (slot == kUsableItemId) {
                has_item = true;
                break;
            }
        }
        if (!has_item) {
            return reject(AiActionPlanCode::missing_item);
        }
        i32 item_x = action.target_x;
        i32 item_y = action.target_y;
        if (action.target_unit_id != 0) {
            const UnitMovementUnit* item_target = find_active_unit(
                *input.movement, action.target_unit_id);
            if (item_target == nullptr || !item_target->active ||
                (item_target->command_state & kUnitCommandDead) != 0) {
                return reject(AiActionPlanCode::target_inactive);
            }
            if (!is_visible_target(input, *item_target)) {
                return reject(AiActionPlanCode::target_not_visible);
            }
            item_x = item_target->x;
            item_y = item_target->y;
        }
        if (!point_inside_map(input.movement->map, item_x, item_y)) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        AiActionPlanResult result{};
        result.packets.push_back(make_unit_order(input.local_owner,
            kItemUseCommand, primary, action.target_unit_id, item_x, item_y,
            false));
        result.code = AiActionPlanCode::okay;
        return result;
    }

    const UnitMovementUnit* target = nullptr;
    u32 target_id = 0;
    i32 target_x = action.target_x;
    i32 target_y = action.target_y;
    switch (action.kind) {
    case AiSemanticActionKind::move:
    case AiSemanticActionKind::attack_move:
        if (action.target_unit_id != 0) {
            return reject(AiActionPlanCode::unexpected_target);
        }
        break;
    case AiSemanticActionKind::attack_unit:
        if (action.target_unit_id == 0) {
            return reject(AiActionPlanCode::missing_target);
        }
        target = find_active_unit(*input.movement, action.target_unit_id);
        if (target == nullptr || !target->active ||
            (target->command_state & kUnitCommandDead) != 0) {
            return reject(AiActionPlanCode::target_inactive);
        }
        if (!is_visible_target(input, *target)) {
            return reject(AiActionPlanCode::target_not_visible);
        }
        if (is_friendly_target(input, *target)) {
            return reject(AiActionPlanCode::target_is_friendly);
        }
        target_id = target->id;
        target_x = target->x;
        target_y = target->y;
        break;
    case AiSemanticActionKind::harvest:
    case AiSemanticActionKind::set_rally:
        if (action.target_unit_id != 0) {
            target = find_active_unit(*input.movement, action.target_unit_id);
            if (target == nullptr || !target->active ||
                (target->command_state & kUnitCommandDead) != 0) {
                return reject(AiActionPlanCode::target_inactive);
            }
            if (!is_visible_target(input, *target)) {
                return reject(AiActionPlanCode::target_not_visible);
            }
            target_id = target->id;
            target_x = target->x;
            target_y = target->y;
        }
        break;
    default:
        return reject(AiActionPlanCode::unsupported_action);
    }

    if (!point_inside_map(input.movement->map, target_x, target_y)) {
        return reject(AiActionPlanCode::point_out_of_bounds);
    }

    if (action.kind == AiSemanticActionKind::harvest && target == nullptr) {
        const u64 tile_count = static_cast<u64>(input.movement->map.width) *
            input.movement->map.height;
        // Prefer the explored projection (AI owners have no current-visibility
        // layer); fall back to current visibility when explored is absent.
        const std::vector<u8>* vision = input.explored_tiles != nullptr ?
            input.explored_tiles : input.visible_tiles;
        if (vision == nullptr || tile_count != vision->size()) {
            return reject(AiActionPlanCode::invalid_visible_tile_count);
        }
        std::size_t compact_index = 0;
        const UnitMovementCell* cell = map_cell_at_world_point(
            input.movement->map, target_x, target_y, compact_index);
        if (cell == nullptr) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        if ((*vision)[compact_index] == 0) {
            return reject(AiActionPlanCode::target_not_visible);
        }
        const bool passable =
            (cell->flags & kMapCellTerrainMask) == kMapCellPassableTerrain &&
            (cell->flags & kMapCellBlockedTerrain) == 0;
        if (!passable ||
            (cell->flags & kMapCellHarvestAmountMask) == 0) {
            return reject(AiActionPlanCode::target_not_harvestable);
        }
    }

    if (action.kind == AiSemanticActionKind::set_rally) {
        if (action.queued) {
            return reject(AiActionPlanCode::queued_flag_unsupported);
        }
        AiActionPlanResult result{};
        result.packets.reserve(units.size());
        for (const UnitMovementUnit* unit : units) {
            if (unit->action_mode_gate == 1u ||
                unit->definition.placement_path_reference_count == 0) {
                return reject(AiActionPlanCode::rally_source_unsupported);
            }
            result.packets.push_back(make_packet(input.local_owner,
                kRallySubtype, *unit, kRallyCommand, target_id,
                static_cast<u32>(target_x), static_cast<u32>(target_y)));
        }
        result.code = AiActionPlanCode::okay;
        return result;
    }

    AiActionPlanResult result{};
    result.packets.reserve(units.size());
    for (const UnitMovementUnit* unit : units) {
        u32 command = 0;
        switch (action.kind) {
        case AiSemanticActionKind::move:
            command = kMoveCommand;
            break;
        case AiSemanticActionKind::attack_move:
        case AiSemanticActionKind::attack_unit:
            // This mirrors the human explicit-attack path: combat-capable
            // units use action 5 while other mobile selected units fall back
            // to ordinary action 4.
            command = supports_action(*unit, kAttackCommand)
                ? kAttackCommand
                : kMoveCommand;
            break;
        case AiSemanticActionKind::harvest:
            command = kHarvestCommand;
            break;
        default:
            return reject(AiActionPlanCode::unsupported_action);
        }
        if (!supports_action(*unit, command)) {
            return reject(AiActionPlanCode::unit_action_unsupported);
        }
        result.packets.push_back(make_unit_order(input.local_owner, command,
            *unit, target_id, target_x, target_y, action.queued));
    }

    result.code = AiActionPlanCode::okay;
    return result;
}

} // namespace ranker
