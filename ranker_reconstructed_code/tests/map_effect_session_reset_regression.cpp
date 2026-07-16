#include "ranker_map_effects.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace ranker {

u32 UnitMovementMapTileIndex(const UnitMovementMap& map, u32 tile_x,
    u32 tile_y) {
    return tile_y * map.stride_tiles + tile_x;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "MAP_EFFECT_SESSION_RESET_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Fixture {
    UnitMovementMap map;
    MapEffectContext context;

    Fixture() {
        map.width = 16;
        map.height = 16;
        map.stride_tiles = 16;
        map.cells.resize(16 * 16);

        context.map = &map;
        context.effects.resize(8);
        context.active_effect_indices = {3, 2, 1};
        context.free_effect_indices = {7, 6, 5, 4};
        for (u32 index = 1; index <= 3; ++index) {
            MapEffectInstance& effect = context.effects[index];
            effect.active = true;
            effect.id = index;
            effect.effect_id = 131;
            effect.x = static_cast<i32>(index * 0x20);
            effect.y = static_cast<i32>(index * 0x20);
            effect.repeat_count = 1;
            effect.raw_record[0] = 0x83;
            MarkMapEffectTileOccupied(context, effect);
        }
    }
};

void test_fresh_session_releases_imported_active_chain() {
    Fixture fixture;
    ResetImportedMapEffectsForSessionMode(fixture.context, 1);

    require(fixture.context.active_effect_indices.empty(),
        "fresh P2P session retained imported map effects");
    require(fixture.context.free_effect_indices ==
            std::vector<u32>({7, 6, 5, 4, 3, 2, 1}),
        "released effects did not become free-list head in traversal order");

    for (u32 index = 1; index <= 3; ++index) {
        const MapEffectInstance& effect = fixture.context.effects[index];
        require(!effect.active && effect.effect_id == 131 &&
                effect.repeat_count == 1 && effect.raw_record[0] == 0x83,
            "release changed the stale payload retained by the original pool");
        const UnitMovementCell* cell = GetMapEffectCell(
            fixture.context, effect.x, effect.y);
        require(cell != nullptr &&
                (cell->visibility_flags & kMapEffectTileFlag) == 0,
            "release did not clear the imported occupancy bit");
    }

    MapEffectInstance* allocated = AllocateMapEffect(fixture.context);
    require(allocated == &fixture.context.effects[1],
        "next allocation did not match the original released-list head");
}

void test_saved_game_mode_preserves_imported_lists() {
    Fixture fixture;
    ResetImportedMapEffectsForSessionMode(fixture.context, 5);

    require(fixture.context.active_effect_indices ==
            std::vector<u32>({3, 2, 1}) &&
            fixture.context.free_effect_indices ==
            std::vector<u32>({7, 6, 5, 4}),
        "saved-game mode changed imported intrusive lists");
    for (u32 index = 1; index <= 3; ++index) {
        const MapEffectInstance& effect = fixture.context.effects[index];
        const UnitMovementCell* cell = GetMapEffectCell(
            fixture.context, effect.x, effect.y);
        require(effect.active && cell != nullptr &&
                (cell->visibility_flags & kMapEffectTileFlag) != 0,
            "saved-game mode released a live imported effect");
    }
}

} // namespace

int main() {
    test_fresh_session_releases_imported_active_chain();
    test_saved_game_mode_preserves_imported_lists();
    std::cout << "MAP_EFFECT_SESSION_RESET_PASS fresh=release mode5=preserve "
                 "free_head=1 stale_payload=preserved\n";
    return EXIT_SUCCESS;
}
