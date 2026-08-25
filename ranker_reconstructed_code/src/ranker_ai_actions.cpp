#include "ranker_ai_actions.h"

#include <algorithm>

namespace ranker {
namespace {

constexpr u32 kUnitProductionSubtype = 0x01u;
constexpr u32 kUnitOrderSubtype = 0x02u;
constexpr u32 kRallySubtype = 0x08u;
constexpr u32 kResearchSubtype = 0x0cu;
constexpr u32 kMoveCommand = 0x04u;
constexpr u32 kAttackCommand = 0x05u;
constexpr u32 kBuildCommand = 0x06u;
constexpr u32 kHarvestCommand = 0x07u;
constexpr u32 kRallyCommand = 0x1fu;
constexpr u32 kQueuedCommandFlag = 0x80000000u;
constexpr u32 kCommandStateMask = 0x00ffffffu;
constexpr u32 kFirstBuildingType = 0x60u;
constexpr u32 kBuildingTypeLimit = 0xaau;
constexpr u32 kMobileUnitTypeLimit = 0x60u;
constexpr u32 kResearchOrderLimit = 0x40u;
constexpr u32 kUnitProductionQueueLimit = 4u;
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
        action.kind == AiSemanticActionKind::cancel_production) {
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
        if (input.visible_tiles == nullptr ||
            tile_count != input.visible_tiles->size()) {
            return reject(AiActionPlanCode::invalid_visible_tile_count);
        }
        std::size_t compact_index = 0;
        const UnitMovementCell* cell = map_cell_at_world_point(
            input.movement->map, target_x, target_y, compact_index);
        if (cell == nullptr) {
            return reject(AiActionPlanCode::point_out_of_bounds);
        }
        if ((*input.visible_tiles)[compact_index] == 0) {
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
