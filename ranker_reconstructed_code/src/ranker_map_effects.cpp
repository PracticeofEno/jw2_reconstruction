#include "ranker_map_effects.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace ranker {
namespace {

constexpr u32 kMapEffectPlacementSourceFlag = 0x20000000;
constexpr u32 kPassivePrimaryEquipmentSlot = 4;
constexpr u32 kPassiveSecondaryEquipmentSlot = 5;
constexpr std::size_t kRawTypeOffset = 0x00;
constexpr std::size_t kRawFlagsOffset = 0x0c;
constexpr std::size_t kRawLinkedUnitOffset = 0x10;
constexpr std::size_t kRawXOffset = 0x24;
constexpr std::size_t kRawYOffset = 0x28;
constexpr std::size_t kRawFrameTimerOffset = 0x2c;
constexpr std::size_t kRawRepeatCountOffset = 0x30;

u32 read_raw_u32(const u8* record, std::size_t offset) {
    u32 value = 0;
    std::memcpy(&value, record + offset, sizeof(value));
    return value;
}

void write_raw_u32(u8* record, std::size_t offset, u32 value) {
    std::memcpy(record + offset, &value, sizeof(value));
}

u32 tile_index(const UnitMovementMap& map, u32 tile_x, u32 tile_y) {
    return UnitMovementMapTileIndex(map, tile_x, tile_y);
}

bool in_viewport(const MapEffectViewport& viewport, i32 x, i32 y) {
    return viewport.left <= x && x < viewport.right &&
        viewport.top <= y && y < viewport.bottom;
}

const MapEffectDefinition* lookup_definition(const MapEffectContext& context,
    u32 effect_id) {
    if (context.callbacks.find_definition == nullptr) {
        return nullptr;
    }
    return context.callbacks.find_definition(context, effect_id);
}

bool linked_effect_still_valid(const MapEffectContext& context,
    const MapEffectInstance& effect) {
    if ((effect.flags & kMapEffectLinkedFlag) == 0) {
        return true;
    }
    if (effect.linked_unit == nullptr ||
        (effect.linked_unit->runtime_flags & 4) != 0) {
        return false;
    }
    if (context.callbacks.linked_unit_keeps_effect != nullptr) {
        return context.callbacks.linked_unit_keeps_effect(
            context, *effect.linked_unit, effect);
    }
    return true;
}

bool unit_has_effect_slot(const UnitMovementUnit& unit, const MapEffectInstance& effect) {
    if (effect.effect_id < 5) {
        return true;
    }
    return std::any_of(unit.item_slots.begin(), unit.item_slots.end(),
        [](u32 slot) { return slot == 0; });
}

bool contains_effect_index(const std::vector<u32>& indices, u32 index) {
    return std::find(indices.begin(), indices.end(), index) != indices.end();
}

void remove_effect_index(std::vector<u32>& indices, u32 index) {
    const auto found = std::find(indices.begin(), indices.end(), index);
    if (found != indices.end()) {
        indices.erase(found);
    }
}

u32 effect_slot_index(const MapEffectContext& context,
    const MapEffectInstance& effect) {
    if (context.effects.empty()) {
        return effect.id;
    }
    const MapEffectInstance* base = context.effects.data();
    const MapEffectInstance* current = &effect;
    if (current < base || current >= base + context.effects.size()) {
        return effect.id;
    }
    return static_cast<u32>(current - base);
}

bool initialize_effect(MapEffectContext& context, MapEffectInstance& effect,
    u32 effect_id, i32 x, i32 y, UnitMovementUnit* linked_unit) {
    const MapEffectDefinition* definition = lookup_definition(context, effect_id);
    const u32 slot_id = effect.id;
    const auto raw_record = effect.raw_record;
    effect = MapEffectInstance{};
    effect.active = true;
    effect.id = slot_id;
    effect.raw_record = raw_record;
    effect.effect_id = effect_id;
    effect.flags = linked_unit != nullptr ? kMapEffectLinkedFlag : 0;
    effect.x = x & ~0x1f;
    effect.y = y & ~0x1f;
    effect.linked_unit = linked_unit;
    effect.linked_unit_raw_offset = linked_unit != nullptr ? linked_unit->id : 0;
    effect.repeat_count = definition != nullptr ?
        std::max<u32>(definition->default_repeat_count, 1) : 1;
    effect.frame_timer = definition != nullptr ? definition->frame_period : 0;
    MarkMapEffectTileOccupied(context, effect);
    return true;
}

UnitMovementPoint find_nearest_tile(const MapEffectContext& context, i32 x, i32 y,
    u32 terrain_class, bool require_matching_terrain) {
    x &= ~0x1f;
    y &= ~0x1f;
    for (u32 radius = 0; radius < kMapEffectSearchRadius; ++radius) {
        const i32 min_x = x - static_cast<i32>(radius * 32);
        const i32 min_y = y - static_cast<i32>(radius * 32);
        const i32 max_x = x + static_cast<i32>(radius * 32);
        const i32 max_y = y + static_cast<i32>(radius * 32);

        for (i32 scan_y = min_y; scan_y <= max_y; scan_y += 32) {
            for (i32 scan_x = min_x; scan_x <= max_x; scan_x += 32) {
                if (CheckMapEffectPlacementTile(context, scan_x, scan_y,
                        terrain_class, require_matching_terrain)) {
                    return {scan_x, scan_y};
                }
            }
        }
    }
    return {x, y};
}

u32 passive_counter_effect_id(u32 value) {
    u32 effect_id = 1;
    if (value > 100) {
        ++effect_id;
    }
    if (value > 500) {
        ++effect_id;
    }
    if (value > 1000) {
        ++effect_id;
    }
    return effect_id;
}

bool spawn_unit_passive_counter_effect(MapEffectContext& context,
    UnitMovementUnit& unit) {
    const u32 value = unit.action_mode;
    if (value == 0) {
        return false;
    }

    MapEffectInstance* effect = HandleMapEffectNearestTileSpawn(
        context, passive_counter_effect_id(value), unit.x, unit.y);
    if (effect == nullptr) {
        return false;
    }

    u32 repeat = value;
    if ((unit.definition.passive_recovery_flags & 2u) == 0) {
        const u32 limit = value >> 2;
        if (limit != 0 && context.callbacks.random_limit != nullptr) {
            repeat += context.callbacks.random_limit(context, limit);
        }
    }
    effect->repeat_count = repeat;
    unit.action_mode = 0;
    return true;
}

bool spawn_unit_passive_slot_effect(MapEffectContext& context, UnitMovementUnit& unit,
    u32& slot_value) {
    if (slot_value == 0) {
        return false;
    }
    if (HandleMapEffectNearestTileSpawn(context, slot_value, unit.x, unit.y) ==
        nullptr) {
        return false;
    }
    slot_value = 0;
    return true;
}

} // namespace

bool HydrateMapEffectRawRecord(MapEffectInstance& effect,
    const u8* record, std::size_t record_size) {
    if (record == nullptr || record_size < kMapEffectRawRecordSize) {
        return false;
    }
    std::copy_n(record, kMapEffectRawRecordSize, effect.raw_record.begin());
    effect.effect_id = read_raw_u32(record, kRawTypeOffset);
    effect.flags = read_raw_u32(record, kRawFlagsOffset);
    effect.linked_unit_raw_offset = read_raw_u32(record, kRawLinkedUnitOffset);
    effect.x = static_cast<i32>(read_raw_u32(record, kRawXOffset));
    effect.y = static_cast<i32>(read_raw_u32(record, kRawYOffset));
    effect.frame_timer = read_raw_u32(record, kRawFrameTimerOffset);
    effect.repeat_count = read_raw_u32(record, kRawRepeatCountOffset);
    effect.linked_unit = nullptr;
    return true;
}

bool StoreMapEffectRawRecord(const MapEffectInstance& effect,
    u8* record, std::size_t record_size) {
    if (record == nullptr || record_size < kMapEffectRawRecordSize) {
        return false;
    }
    std::copy(effect.raw_record.begin(), effect.raw_record.end(), record);
    write_raw_u32(record, kRawTypeOffset, effect.effect_id);
    write_raw_u32(record, kRawFlagsOffset, effect.flags);
    write_raw_u32(record, kRawLinkedUnitOffset,
        effect.active && effect.linked_unit != nullptr ?
            effect.linked_unit->id : effect.linked_unit_raw_offset);
    write_raw_u32(record, kRawXOffset, static_cast<u32>(effect.x));
    write_raw_u32(record, kRawYOffset, static_cast<u32>(effect.y));
    write_raw_u32(record, kRawFrameTimerOffset, effect.frame_timer);
    write_raw_u32(record, kRawRepeatCountOffset, effect.repeat_count);
    return true;
}

UnitMovementCell* GetMapEffectCell(MapEffectContext& context, i32 x, i32 y) {
    if (context.map == nullptr || x < 0 || y < 0) {
        return nullptr;
    }
    const u32 tile_x = static_cast<u32>(x) >> 5;
    const u32 tile_y = static_cast<u32>(y) >> 5;
    if (tile_x >= context.map->width || tile_y >= context.map->height) {
        return nullptr;
    }
    const u32 index = tile_index(*context.map, tile_x, tile_y);
    if (index >= context.map->cells.size()) {
        return nullptr;
    }
    return &context.map->cells[index];
}

const UnitMovementCell* GetMapEffectCell(const MapEffectContext& context, i32 x, i32 y) {
    return GetMapEffectCell(const_cast<MapEffectContext&>(context), x, y);
}

bool CheckMapEffectPlacementTile(const MapEffectContext& context, i32 x, i32 y,
    u32 terrain_class, bool require_matching_terrain) {
    const UnitMovementCell* cell = GetMapEffectCell(context, x, y);
    if (cell == nullptr) {
        return false;
    }
    const u32 source_flags = cell->alternate_flags;
    if ((source_flags & kMapEffectPlacementSourceFlag) == 0) {
        return false;
    }
    if ((cell->flags & kMapCellTerrainMask) != 0) {
        return false;
    }
    if ((cell->visibility_flags & (kMapEffectTileFlag | kMapEffectBlockedTileFlag)) != 0) {
        return false;
    }
    if (require_matching_terrain &&
        (source_flags & kMapEffectTerrainClassMask) != terrain_class) {
        return false;
    }
    return true;
}

UnitMovementPoint FindNearestMapEffectTile(const MapEffectContext& context, i32 x,
    i32 y) {
    return find_nearest_tile(context, x, y, 0, false);
}

UnitMovementPoint FindTerrainMatchedMapEffectTile(
    const MapEffectContext& context, i32 x, i32 y, u32 terrain_class) {
    return find_nearest_tile(context, x, y, terrain_class, true);
}

void MarkMapEffectTileOccupied(MapEffectContext& context, const MapEffectInstance& effect) {
    UnitMovementCell* cell = GetMapEffectCell(context, effect.x, effect.y);
    if (cell != nullptr) {
        cell->visibility_flags |= kMapEffectTileFlag;
    }
}

void ClearMapEffectTileOccupied(MapEffectContext& context, const MapEffectInstance& effect) {
    UnitMovementCell* cell = GetMapEffectCell(context, effect.x, effect.y);
    if (cell != nullptr) {
        cell->visibility_flags &= ~kMapEffectTileFlag;
    }
}

MapEffectInstance* AllocateMapEffect(MapEffectContext& context) {
    while (!context.free_effect_indices.empty()) {
        const u32 index = context.free_effect_indices.back();
        context.free_effect_indices.pop_back();
        if (index != 0 && index < context.effects.size() &&
            !context.effects[index].active) {
            context.effects[index].active = true;
            context.active_effect_indices.insert(
                context.active_effect_indices.begin(), index);
            return &context.effects[index];
        }
    }
    return nullptr;
}

void ReleaseMapEffect(MapEffectContext& context, MapEffectInstance& effect) {
    const u32 index = effect_slot_index(context, effect);
    ClearMapEffectTileOccupied(context, effect);
    if (index >= context.effects.size()) {
        return;
    }
    if (!context.effects[index].active) {
        return;
    }

    remove_effect_index(context.active_effect_indices, index);
    context.effects[index].active = false;
    if (!contains_effect_index(context.free_effect_indices, index)) {
        context.free_effect_indices.push_back(index);
    }
}

MapEffectInstance* HandleMapEffectNearestTileSpawn(MapEffectContext& context,
    u32 effect_id, i32 x, i32 y, UnitMovementUnit* linked_unit) {
    MapEffectInstance* effect = AllocateMapEffect(context);
    if (effect == nullptr) {
        return nullptr;
    }
    const UnitMovementPoint point = FindNearestMapEffectTile(context, x, y);
    initialize_effect(context, *effect, effect_id, point.x, point.y, linked_unit);
    return effect;
}

MapEffectInstance* HandleMapEffectMatchingTerrainSpawn(MapEffectContext& context,
    u32 effect_id, i32 x, i32 y, u32 terrain_class, UnitMovementUnit* linked_unit) {
    MapEffectInstance* effect = AllocateMapEffect(context);
    if (effect == nullptr) {
        return nullptr;
    }
    const UnitMovementPoint point =
        FindTerrainMatchedMapEffectTile(context, x, y, terrain_class);
    initialize_effect(context, *effect, effect_id, point.x, point.y, linked_unit);
    return effect;
}

MapEffectInstance* FindNearbyInteractableMapEffectForUnit(MapEffectContext& context,
    const UnitMovementUnit& unit, u32 max_distance) {
    const i32 aligned_x = unit.x & ~0x1f;
    const i32 aligned_y = unit.y & ~0x1f;
    for (const u32 index : context.active_effect_indices) {
        if (index >= context.effects.size()) {
            continue;
        }
        MapEffectInstance& effect = context.effects[index];
        if (!effect.active || (effect.flags & kMapEffectLinkedFlag) != 0) {
            continue;
        }
        if (context.callbacks.can_unit_interact != nullptr &&
            !context.callbacks.can_unit_interact(context, unit, effect)) {
            continue;
        }
        const u32 dx = static_cast<u32>(std::abs(aligned_x - effect.x));
        const u32 dy = static_cast<u32>(std::abs(aligned_y - effect.y));
        if (std::max(dx, dy) <= max_distance && unit_has_effect_slot(unit, effect)) {
            return &effect;
        }
    }
    return nullptr;
}

u32 GetMapEffectTerrainClassAtWorldPoint(const MapEffectContext& context, i32 x, i32 y) {
    const UnitMovementCell* cell = GetMapEffectCell(context, x, y);
    if (cell == nullptr) {
        return 0;
    }
    return cell->alternate_flags & kMapEffectTerrainClassMask;
}

bool HandleCompletionTerrainEffectSpawnTick(MapEffectContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 period) {
    ++unit.action_mode;
    if (period != 0 && unit.action_mode < period) {
        return false;
    }
    const u32 terrain_class = GetMapEffectTerrainClassAtWorldPoint(context, unit.x, unit.y);
    const i32 effect_x = unit.x + unit.definition.completion_effect_offset_x;
    const i32 effect_y = unit.y + unit.definition.completion_effect_offset_y;
    HandleMapEffectMatchingTerrainSpawn(context, effect_id, effect_x, effect_y,
        terrain_class);
    return true;
}

void HandleVisibleMapEffectSpriteQueue(MapEffectContext& context) {
    for (const u32 index : context.active_effect_indices) {
        if (index >= context.effects.size()) {
            continue;
        }
        const MapEffectInstance& effect = context.effects[index];
        if (!effect.active || !in_viewport(context.viewport, effect.x, effect.y)) {
            continue;
        }
        const UnitMovementCell* cell = GetMapEffectCell(context, effect.x, effect.y);
        if (cell == nullptr) {
            continue;
        }
        if (context.viewport.require_visible_tile &&
            (cell->visibility_flags & kMapEffectVisibleTileFlag) == 0) {
            continue;
        }
        if ((cell->visibility_flags & kMapEffectRenderableTileFlag) != 0) {
            DrawMapEffectSprite(context, effect);
        }
    }
}

void DrawMapEffectSprite(MapEffectContext& context, const MapEffectInstance& effect) {
    if (context.callbacks.render_effect != nullptr) {
        context.callbacks.render_effect(context, effect);
    }
}

void HandleMapEffectTimerTick(MapEffectContext& context) {
    if ((context.frame_counter & 0x1fu) != 0) {
        return;
    }

    std::size_t active_position = 0;
    while (active_position < context.active_effect_indices.size()) {
        const u32 index = context.active_effect_indices[active_position];
        if (index >= context.effects.size()) {
            context.active_effect_indices.erase(
                context.active_effect_indices.begin() +
                    static_cast<std::ptrdiff_t>(active_position));
            continue;
        }
        MapEffectInstance& effect = context.effects[index];
        if (!effect.active) {
            context.active_effect_indices.erase(
                context.active_effect_indices.begin() +
                    static_cast<std::ptrdiff_t>(active_position));
            continue;
        }
        if (!linked_effect_still_valid(context, effect)) {
            effect.flags &= ~kMapEffectLinkedFlag;
        }

        const MapEffectDefinition* definition = lookup_definition(context, effect.effect_id);
        if (definition == nullptr || definition->frame_period == 0) {
            ++active_position;
            continue;
        }

        --effect.frame_timer;
        if (effect.frame_timer != 0) {
            ++active_position;
            continue;
        }

        effect.frame_timer = definition->frame_period;
        --effect.repeat_count;
        if (effect.repeat_count == 0) {
            ReleaseMapEffect(context, effect);
            continue;
        }

        if (active_position < context.active_effect_indices.size() &&
            context.active_effect_indices[active_position] == index) {
            ++active_position;
        }
    }
}

bool StartUnitProgressMapEffect(MapEffectContext& context, UnitMovementUnit& unit,
    u32 effect_id) {
    if (effect_id == 0) {
        return false;
    }

    u32 spawn_effect_id = effect_id;
    u32 repeat = 0;
    bool update_repeat = false;
    if (effect_id < 5) {
        repeat = unit.action_mode;
        if (repeat == 0) {
            return false;
        }
        if (repeat > 50) {
            repeat -= 50;
        }
        spawn_effect_id = passive_counter_effect_id(repeat);
        update_repeat = true;
    }

    MapEffectInstance* effect = HandleMapEffectNearestTileSpawn(
        context, spawn_effect_id, unit.path_target_x, unit.path_target_y);
    if (effect == nullptr) {
        return false;
    }

    if (update_repeat) {
        effect->repeat_count = std::max<u32>(repeat, 1);
        const u32 before = unit.action_mode;
        unit.action_mode -= repeat;
        if (before < repeat) {
            unit.action_mode = 0;
        }
    }
    return true;
}

void SpawnUnitPassiveMapEffects(MapEffectContext& context, UnitMovementUnit& unit) {
    if (unit.owner_id > 7 && (unit.runtime_flags & 0x40000) == 0) {
        if (unit.definition.lifecycle_class == 1) {
            unit.action_mode = unit.definition.passive_map_effect_seed;
            if (context.callbacks.on_random_ambient_spawn_tick != nullptr) {
                context.callbacks.on_random_ambient_spawn_tick(context, unit);
            }
        }
        if (unit.action_mode != 0) {
            spawn_unit_passive_counter_effect(context, unit);
        }
    }

    if (unit.equipment_slots.size() > kPassivePrimaryEquipmentSlot) {
        spawn_unit_passive_slot_effect(context, unit,
            unit.equipment_slots[kPassivePrimaryEquipmentSlot]);
    }
    if (unit.equipment_slots.size() > kPassiveSecondaryEquipmentSlot) {
        spawn_unit_passive_slot_effect(context, unit,
            unit.equipment_slots[kPassiveSecondaryEquipmentSlot]);
    }
    for (std::size_t slot = 0; slot < unit.item_slots.size(); ++slot) {
        spawn_unit_passive_slot_effect(context, unit, unit.item_slots[slot]);
        if (slot < unit.equipment_slots.size()) {
            unit.equipment_slots[slot] = unit.item_slots[slot];
        }
    }
}

} // namespace ranker
