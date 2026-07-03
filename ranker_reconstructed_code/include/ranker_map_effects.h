#pragma once

#include "ranker_unit_movement.h"

#include <vector>

namespace ranker {

constexpr u32 kMapEffectTileFlag = 0x40000000;
constexpr u32 kMapEffectBlockedTileFlag = 0x20000000;
constexpr u32 kMapEffectRenderableTileFlag = 0x10000000;
constexpr u32 kMapEffectVisibleTileFlag = 0x08000000;
constexpr u32 kMapEffectLinkedFlag = 0x00000001;
constexpr u32 kMapEffectTerrainClassMask = 0x1c000000;
constexpr u32 kMapEffectSearchRadius = 0x0f;

struct MapEffectViewport {
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
    bool require_visible_tile = false;
};

struct MapEffectDefinition {
    u32 id = 0;
    u32 frame_period = 0;
    u32 default_repeat_count = 1;
    u32 terrain_class = 0;
    u32 sprite_frame_index = 0;
};

struct MapEffectInstance {
    bool active = false;
    u32 id = 0;
    u32 effect_id = 0;
    u32 flags = 0;
    u32 frame_timer = 0;
    u32 repeat_count = 0;
    i32 x = 0;
    i32 y = 0;
    UnitMovementUnit* linked_unit = nullptr;
};

struct MapEffectContext;

using MapEffectRenderCallback = void (*)(MapEffectContext& context,
    const MapEffectInstance& effect);
using MapEffectDefinitionLookup = const MapEffectDefinition* (*)(
    const MapEffectContext& context, u32 effect_id);
using MapEffectUnitPredicate = bool (*)(const MapEffectContext& context,
    const UnitMovementUnit& unit, const MapEffectInstance& effect);
using MapEffectLinkedUnitPredicate = bool (*)(const MapEffectContext& context,
    const UnitMovementUnit& unit, const MapEffectInstance& effect);
using MapEffectUnitCallback = void (*)(MapEffectContext& context,
    UnitMovementUnit& unit);
using MapEffectRandomLimitCallback = u32 (*)(MapEffectContext& context, u32 limit);

struct MapEffectCallbacks {
    MapEffectRenderCallback render_effect = nullptr;
    MapEffectDefinitionLookup find_definition = nullptr;
    MapEffectUnitPredicate can_unit_interact = nullptr;
    MapEffectLinkedUnitPredicate linked_unit_keeps_effect = nullptr;
    MapEffectUnitCallback on_random_ambient_spawn_tick = nullptr;
    MapEffectRandomLimitCallback random_limit = nullptr;
};

struct MapEffectContext {
    UnitMovementMap* map = nullptr;
    MapEffectCallbacks callbacks;
    MapEffectViewport viewport;
    std::vector<MapEffectInstance> effects;
    std::vector<u32> active_effect_indices;
    std::vector<u32> free_effect_indices;
    u32 frame_counter = 0;
};

UnitMovementCell* GetMapEffectCell(MapEffectContext& context, i32 x, i32 y);
const UnitMovementCell* GetMapEffectCell(const MapEffectContext& context, i32 x, i32 y);
bool CheckMapEffectPlacementTile(const MapEffectContext& context, i32 x, i32 y,
    u32 terrain_class = 0, bool require_matching_terrain = false);
UnitMovementPoint FindNearestMapEffectTile(const MapEffectContext& context, i32 x,
    i32 y);
UnitMovementPoint FindTerrainMatchedMapEffectTile(
    const MapEffectContext& context, i32 x, i32 y, u32 terrain_class);
void MarkMapEffectTileOccupied(MapEffectContext& context, const MapEffectInstance& effect);
void ClearMapEffectTileOccupied(MapEffectContext& context, const MapEffectInstance& effect);
MapEffectInstance* AllocateMapEffect(MapEffectContext& context);
void ReleaseMapEffect(MapEffectContext& context, MapEffectInstance& effect);
MapEffectInstance* HandleMapEffectNearestTileSpawn(MapEffectContext& context,
    u32 effect_id, i32 x, i32 y, UnitMovementUnit* linked_unit = nullptr);
MapEffectInstance* HandleMapEffectMatchingTerrainSpawn(MapEffectContext& context,
    u32 effect_id, i32 x, i32 y, u32 terrain_class, UnitMovementUnit* linked_unit = nullptr);
MapEffectInstance* FindNearbyInteractableMapEffectForUnit(MapEffectContext& context,
    const UnitMovementUnit& unit, u32 max_distance = 0xc0);
u32 GetMapEffectTerrainClassAtWorldPoint(const MapEffectContext& context, i32 x, i32 y);
bool HandleCompletionTerrainEffectSpawnTick(MapEffectContext& context,
    UnitMovementUnit& unit, u32 effect_id, u32 period);
void HandleVisibleMapEffectSpriteQueue(MapEffectContext& context);
void DrawMapEffectSprite(MapEffectContext& context, const MapEffectInstance& effect);
void HandleMapEffectTimerTick(MapEffectContext& context);
bool StartUnitProgressMapEffect(MapEffectContext& context, UnitMovementUnit& unit,
    u32 effect_id);
void SpawnUnitPassiveMapEffects(MapEffectContext& context, UnitMovementUnit& unit);

} // namespace ranker
