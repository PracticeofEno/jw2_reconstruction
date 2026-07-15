#include "ranker_map_effects.h"

#include <cassert>
#include <iostream>

namespace ranker {

u32 UnitMovementMapStrideTiles(const UnitMovementMap& map) {
    return map.stride_tiles != 0 ? map.stride_tiles : map.width;
}

u32 UnitMovementMapTileIndex(const UnitMovementMap& map, u32 tile_x,
    u32 tile_y) {
    return tile_y * UnitMovementMapStrideTiles(map) + tile_x;
}

} // namespace ranker

namespace {

ranker::MapEffectDefinition g_zero_definition{0, 1, 1, 0, 0};
unsigned g_render_count = 0;

const ranker::MapEffectDefinition* find_definition(
    const ranker::MapEffectContext&, unsigned effect_id) {
    return effect_id == 0 ? &g_zero_definition : nullptr;
}

void render_effect(ranker::MapEffectContext&,
    const ranker::MapEffectInstance& effect) {
    assert(effect.active);
    assert(effect.effect_id == 0);
    ++g_render_count;
}

} // namespace

int main() {
    using namespace ranker;

    UnitMovementMap map;
    map.width = 1;
    map.height = 1;
    map.stride_tiles = 1;
    map.cells.resize(1);
    map.cells[0].alternate_flags = 0x20000000u;
    map.cells[0].visibility_flags = kMapEffectRenderableTileFlag;

    MapEffectContext context;
    context.map = &map;
    context.effects.resize(2);
    context.effects[0].id = 0;
    context.effects[1].id = 1;
    context.free_effect_indices.push_back(1);
    context.callbacks.find_definition = find_definition;
    context.callbacks.render_effect = render_effect;
    context.viewport = {0, 0, 32, 32, false};

    MapEffectInstance* effect =
        HandleMapEffectNearestTileSpawn(context, 0, 0, 0);
    assert(effect != nullptr);
    assert(effect->active);
    assert(effect->effect_id == 0);
    assert(context.active_effect_indices.size() == 1);
    assert(context.active_effect_indices.front() == 1);
    assert(context.free_effect_indices.empty());
    assert((map.cells[0].visibility_flags & kMapEffectTileFlag) != 0);

    HandleVisibleMapEffectSpriteQueue(context);
    assert(g_render_count == 1);

    context.frame_counter = 0x20;
    HandleMapEffectTimerTick(context);
    assert(context.active_effect_indices.empty());
    assert(context.free_effect_indices.size() == 1);
    assert(context.free_effect_indices.back() == 1);
    assert((map.cells[0].visibility_flags & kMapEffectTileFlag) == 0);

    UnitMovementUnit source;
    source.x = 0;
    source.y = 0;
    assert(HandleCompletionTerrainEffectSpawnTick(context, source, 0, 1));
    assert(context.active_effect_indices.size() == 1);
    assert(context.effects[1].active);
    assert(context.effects[1].effect_id == 0);

    std::cout << "MAP_EFFECT_ZERO_ID_RUNTIME_PASS slot=1 spawn/render/release/reuse\n";
    return 0;
}
