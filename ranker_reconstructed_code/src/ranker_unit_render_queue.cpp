#include "ranker_unit_render_queue.h"

#include <algorithm>

namespace ranker {
namespace {

u32 tile_index(const UnitVisibilityGrid& grid, u32 tile_x, u32 tile_y) {
    return tile_y * grid.width + tile_x;
}

u32* tile_cell(UnitVisibilityGrid& grid, u32 tile_x, u32 tile_y) {
    if (tile_x >= grid.width || tile_y >= grid.height) {
        return nullptr;
    }
    const u32 index = tile_index(grid, tile_x, tile_y);
    if (index >= grid.cells.size()) {
        return nullptr;
    }
    return &grid.cells[index];
}

u32* tile_cell(std::vector<u32>& cells, const UnitVisibilityGrid& grid,
    u32 tile_x, u32 tile_y) {
    if (tile_x >= grid.width || tile_y >= grid.height) {
        return nullptr;
    }
    const u32 index = tile_index(grid, tile_x, tile_y);
    if (index >= cells.size()) {
        return nullptr;
    }
    return &cells[index];
}

const u32* tile_cell(const UnitVisibilityGrid& grid, u32 tile_x, u32 tile_y) {
    if (tile_x >= grid.width || tile_y >= grid.height) {
        return nullptr;
    }
    const u32 index = tile_index(grid, tile_x, tile_y);
    if (index >= grid.cells.size()) {
        return nullptr;
    }
    return &grid.cells[index];
}

u32 render_class_index(u32 render_class) {
    return std::min<u32>(render_class, 7);
}

u32 unit_sort_key(const UnitRenderQueueContext& context, const UnitRenderItem& item) {
    const u32 class_index = render_class_index(item.render_class);
    if (item.definition_kind == 2) {
        const i32 center_x = item.x + item.center_offset_x + (item.center_width >> 1);
        const i32 center_y = item.y + item.center_offset_y + (item.center_height >> 1);
        if (item.type_id == 0x6a) {
            return static_cast<u32>(center_y * (1 << kRenderSortRowShift) + center_x) +
                0x20000000;
        }
        return static_cast<u32>(center_y * (1 << kRenderSortRowShift) + center_x) +
            context.unit_sort_bias_by_class[class_index];
    }
    return static_cast<u32>(item.y * (1 << kRenderSortRowShift) + item.x) +
        context.unit_sort_bias_by_class[class_index];
}

u32 effect_sort_key(const UnitRenderQueueContext& context, const UnitRenderItem& item,
    u32 render_class) {
    const u32 class_index = render_class_index(render_class);
    return static_cast<u32>(item.y * (1 << kRenderSortRowShift) + item.x) +
        context.effect_sort_bias_by_class[class_index];
}

void push_entry(UnitRenderQueueContext& context, const UnitRenderItem& item,
    u32 render_class, u32 layer, u32 sort_key) {
    context.queued_entries.push_back(
        UnitRenderQueueEntry{item.unit, item.type_id, render_class, layer, sort_key});
    if (context.callbacks.on_queue_entry != nullptr) {
        context.callbacks.on_queue_entry(context, item);
    }
}

void push_entry(UnitRenderQueueContext& context, const UnitRenderItem& item,
    u32 layer, u32 sort_key) {
    push_entry(context, item, item.render_class, layer, sort_key);
}

bool unit_individual_visibility_gate(const UnitRenderQueueContext& context,
    const UnitRenderItem& item, u32 tile_x, u32 tile_y) {
    const u32 bit = context.local_owner_id;
    if (bit < 32 && item.owner_id < context.owner_visibility_masks.size() &&
        (context.owner_visibility_masks[item.owner_id] & (1u << bit)) != 0) {
        return true;
    }
    if (bit + 0x12u >= 32) {
        return false;
    }
    const u32* cell = tile_cell(context.visibility, tile_x, tile_y);
    return cell != nullptr && ((*cell & (1u << (bit + 0x12u))) != 0);
}

void mark_fog_blocked(UnitRenderQueueContext& context, const UnitRenderItem& item,
    u32 tile_x, u32 tile_y) {
    u32* fog_blocked_cell = context.visibility.fog_blocked_cells != nullptr ?
        tile_cell(*context.visibility.fog_blocked_cells,
            context.visibility, tile_x, tile_y) :
        tile_cell(context.visibility, tile_x, tile_y);
    if (fog_blocked_cell != nullptr) {
        *fog_blocked_cell &= kMapTileFogMaskPreserve;
    }
    if (context.callbacks.on_fog_blocked_unit != nullptr) {
        context.callbacks.on_fog_blocked_unit(context, item);
    }
}

} // namespace

bool IsUnitRenderItemInViewport(const UnitRenderViewport& viewport,
    const UnitRenderItem& item) {
    return viewport.left <= item.x && item.x < viewport.right &&
        viewport.top <= item.y && item.y < viewport.bottom;
}

bool IsMapTileVisible(const UnitVisibilityGrid& grid, u32 tile_x, u32 tile_y) {
    const u32* cell = tile_cell(grid, tile_x, tile_y);
    if (cell == nullptr) {
        return false;
    }
    if (grid.require_revealed && ((*cell & kMapTileRevealed) == 0)) {
        return false;
    }
    return (*cell & kMapTileVisible) != 0;
}

bool IsUnitRenderItemIndividuallyVisibleToLocal(
    const UnitRenderQueueContext& context, const UnitRenderItem& item) {
    const u32 tile_x = static_cast<u32>(item.x) >> 5;
    const u32 tile_y = static_cast<u32>(item.y) >> 5;
    return unit_individual_visibility_gate(context, item, tile_x, tile_y);
}

void ProcessVisibleUnitRenderQueue(UnitRenderQueueContext& context) {
    for (UnitRenderItem& item : context.units) {
        if ((item.runtime_flags & kUnitRenderHiddenFlag) != 0) {
            continue;
        }
        if (item.definition_kind != 2 && !IsUnitRenderItemInViewport(context.viewport, item)) {
            continue;
        }

        const u32 tile_x = static_cast<u32>(item.x) >> 5;
        const u32 tile_y = static_cast<u32>(item.y) >> 5;
        u32* cell = tile_cell(context.visibility, tile_x, tile_y);
        if (cell == nullptr) {
            continue;
        }
        if (context.visibility.require_revealed && ((*cell & kMapTileRevealed) == 0)) {
            if (((*cell & kMapTileVisible) != 0) && ((item.command_flags & 0x40) != 0)) {
                mark_fog_blocked(context, item, tile_x, tile_y);
            }
            continue;
        }
        if ((*cell & kMapTileVisible) == 0) {
            continue;
        }
        if (item.definition_kind == 2 && (item.command_flags & 0x40) != 0 &&
            !IsUnitRenderItemIndividuallyVisibleToLocal(context, item)) {
            mark_fog_blocked(context, item, tile_x, tile_y);
            continue;
        }

        const u32 class_index = render_class_index(item.render_class);
        push_entry(context, item, context.unit_layer_by_class[class_index],
            unit_sort_key(context, item));
    }
}

void ProcessVisibleEffectRenderQueue(UnitRenderQueueContext& context) {
    for (UnitRenderItem& item : context.effects) {
        if (item.effect_state == 1 || item.command_state == 0x10000077) {
            continue;
        }
        if (!IsUnitRenderItemInViewport(context.viewport, item)) {
            continue;
        }
        const u32 tile_x = static_cast<u32>(item.x) >> 5;
        const u32 tile_y = static_cast<u32>(item.y) >> 5;
        if (!IsMapTileVisible(context.visibility, tile_x, tile_y)) {
            continue;
        }

        u32 render_class = item.render_class;
        if ((item.command_state & 0x40000000) == 0) {
            render_class = 5;
        }
        const u32 class_index = render_class_index(render_class);
        push_entry(context, item, render_class, context.effect_layer_by_class[class_index],
            effect_sort_key(context, item, render_class));
    }
}

void DispatchUnitRenderByType(UnitRenderQueueContext& context, const UnitRenderItem& item,
    i32 screen_x, i32 screen_y) {
    const u32 type_index = std::min<u32>(item.type_id, kUnitRenderDispatchCount - 1);
    UnitRenderDispatchCallback callback = context.callbacks.dispatch_by_type[type_index];
    if (callback != nullptr) {
        callback(context, item, screen_x, screen_y);
    }
}

} // namespace ranker
