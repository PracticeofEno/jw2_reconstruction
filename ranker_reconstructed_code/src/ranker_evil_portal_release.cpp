#include "ranker_unit_commands.h"

#include <cstddef>

namespace ranker {

namespace {

constexpr u32 kEvilPortalType = 0x9b;

} // namespace

bool RecoverLegacyEvilPortalReleasePoint(
    const UnitCommandContext& context, const UnitMovementUnit& builder,
    const UnitMovementUnit& portal, UnitMovementPoint requested_point,
    UnitMovementPoint& resolved_point) {
    if (portal.type_id != kEvilPortalType || context.movement == nullptr) {
        return false;
    }

    const i32 requested_tile_x = requested_point.x >> 5;
    const i32 requested_tile_y = requested_point.y >> 5;
    const i32 portal_tile_x = portal.x >> 5;
    const i32 portal_tile_y = portal.y >> 5;
    const UnitMovementMap& map = context.movement->map;
    if (requested_tile_x < 0 || requested_tile_y < 0 || portal_tile_x < 0 ||
        portal_tile_y < 0 || map.stride_tiles == 0 ||
        static_cast<u32>(requested_tile_x) >= map.width ||
        static_cast<u32>(requested_tile_y) >= map.height ||
        static_cast<u32>(portal_tile_x) >= map.width ||
        static_cast<u32>(portal_tile_y) >= map.height) {
        return false;
    }

    const auto cell_at = [&](i32 tile_x, i32 tile_y)
            -> const UnitMovementCell* {
        const std::size_t index = static_cast<std::size_t>(tile_y) *
            map.stride_tiles + static_cast<u32>(tile_x);
        return index < map.cells.size() ? &map.cells[index] : nullptr;
    };
    const UnitMovementCell* requested_cell =
        cell_at(requested_tile_x, requested_tile_y);
    const UnitMovementCell* portal_cell = cell_at(portal_tile_x, portal_tile_y);
    if (requested_cell == nullptr || portal_cell == nullptr ||
        (requested_cell->alternate_flags & 0x1c000000u) !=
            (portal_cell->alternate_flags & 0x1c000000u)) {
        return false;
    }

    const u32 decoration_flags = requested_cell->alternate_flags;
    bool decoration_allows_entry = false;
    switch (builder.definition.movement_class) {
    case 0:
        decoration_allows_entry =
            (decoration_flags & 0x20000000u) != 0 &&
            (decoration_flags & 0x60000000u) != 0;
        break;
    case 2:
        decoration_allows_entry = (decoration_flags & 0x60000000u) != 0;
        break;
    case 3:
        decoration_allows_entry = true;
        break;
    case 4:
        decoration_allows_entry = (decoration_flags & 0x40000000u) != 0;
        break;
    default:
        break;
    }
    if (!decoration_allows_entry) {
        return false;
    }

    // CheckUnitCanEnterTerrainCell bypasses both terrain and brush occupancy
    // for the original movement-flag shortcut (raw unit +0xac bit zero).
    const bool command_shortcut =
        builder.definition.movement_class == 4 ||
        ((builder.definition.movement_class == 0 ||
             builder.definition.movement_class == 2) &&
            (builder.movement_flags & 1u) != 0);
    if (!command_shortcut && (requested_cell->flags & 0x700u) != 0) {
        return false;
    }

    if (!command_shortcut &&
        (requested_cell->visibility_flags & 0x20000000u) != 0) {
        // The owner-production route probe temporarily ORs the same raw
        // 0x20000000 bit into a candidate footprint. The original portal
        // trace accepts the requested exit, while the reconstruction can
        // observe that typed overlay long enough to choose the preceding
        // x-tile. Never bypass a real registered structure footprint.
        for (const UnitMovementUnit* candidate : context.movement->active_units) {
            if (candidate == nullptr || !candidate->active ||
                !candidate->footprint_registered ||
                (candidate->runtime_flags & 4u) != 0 || candidate->x < 0 ||
                candidate->y < 0) {
                continue;
            }
            const u32 width = candidate->definition.footprint_width_tiles;
            const u32 height = candidate->definition.footprint_height_tiles;
            if (width == 0 || height == 0) {
                continue;
            }
            const i32 left = candidate->x >> 5;
            const i32 top = candidate->y >> 5;
            if (requested_tile_x >= left && requested_tile_y >= top &&
                static_cast<u32>(requested_tile_x - left) < width &&
                static_cast<u32>(requested_tile_y - top) < height) {
                return false;
            }
        }
    }

    const UnitMovementPoint aligned_requested{
        requested_tile_x * 32, requested_tile_y * 32};
    if (resolved_point.x == aligned_requested.x &&
        resolved_point.y == aligned_requested.y) {
        return false;
    }
    resolved_point = aligned_requested;
    return true;
}

} // namespace ranker
