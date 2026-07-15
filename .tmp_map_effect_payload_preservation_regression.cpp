#include "ranker_map_effects.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void put_u32(std::array<u8, ranker::kMapEffectRawRecordSize>& raw,
    std::size_t offset, u32 value) {
    std::memcpy(raw.data() + offset, &value, sizeof(value));
}

u32 get_u32(const std::array<u8, ranker::kMapEffectRawRecordSize>& raw,
    std::size_t offset) {
    u32 value = 0;
    std::memcpy(&value, raw.data() + offset, sizeof(value));
    return value;
}

std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "product source file must be readable");
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::string function_range(const std::string& source, const char* start,
    const char* end) {
    const std::size_t begin = source.find(start);
    const std::size_t finish = source.find(end, begin);
    require(begin != std::string::npos && finish != std::string::npos &&
            finish > begin,
        "product function range must be found");
    return source.substr(begin, finish - begin);
}

const ranker::MapEffectDefinition* find_definition(
    const ranker::MapEffectContext&, u32 effect_id) {
    static const ranker::MapEffectDefinition definition{7, 3, 5, 0, 0};
    return effect_id == definition.id ? &definition : nullptr;
}

std::array<u8, ranker::kMapEffectRawRecordSize> make_raw_fixture() {
    std::array<u8, ranker::kMapEffectRawRecordSize> raw{};
    for (std::size_t index = 0; index < raw.size(); ++index) {
        raw[index] = static_cast<u8>(0x40u + index);
    }
    put_u32(raw, 0x00, 0x55);
    put_u32(raw, 0x0c, 0xa5);
    put_u32(raw, 0x10, 0x12345678);
    put_u32(raw, 0x24, 0xffffffe0);
    put_u32(raw, 0x28, 0x00000060);
    put_u32(raw, 0x2c, 7);
    put_u32(raw, 0x30, 9);
    return raw;
}

void test_raw_record_roundtrip() {
    using namespace ranker;

    const auto fixture = make_raw_fixture();
    MapEffectInstance effect{};
    effect.id = 1;
    require(HydrateMapEffectRawRecord(
                effect, fixture.data(), fixture.size()),
        "raw record hydration must succeed");
    require(effect.id == 1 && !effect.active,
        "hydration must not change physical identity or list membership");
    require(effect.effect_id == 0x55 && effect.flags == 0xa5 &&
            effect.linked_unit_raw_offset == 0x12345678 &&
            effect.x == -32 && effect.y == 96 &&
            effect.frame_timer == 7 && effect.repeat_count == 9,
        "all typed payload fields must hydrate for a free node");
    require(effect.linked_unit == nullptr,
        "unresolved raw +0x10 must not fabricate a host pointer");

    effect.effect_id = 0x66;
    effect.flags = 0;
    effect.x = 0x120;
    effect.y = -0x40;
    effect.frame_timer = 11;
    effect.repeat_count = 13;
    std::array<u8, kMapEffectRawRecordSize> stored{};
    require(StoreMapEffectRawRecord(effect, stored.data(), stored.size()),
        "raw record store must succeed");
    for (const std::size_t offset :
            {std::size_t{0x04}, std::size_t{0x08}, std::size_t{0x14},
             std::size_t{0x18}, std::size_t{0x1c}, std::size_t{0x20}}) {
        require(get_u32(stored, offset) == get_u32(fixture, offset),
            "untyped raw words must survive a typed store");
    }
    require(get_u32(stored, 0x00) == 0x66 &&
            get_u32(stored, 0x0c) == 0 &&
            get_u32(stored, 0x10) == 0x12345678 &&
            get_u32(stored, 0x24) == 0x120 &&
            get_u32(stored, 0x28) == 0xffffffc0 &&
            get_u32(stored, 0x2c) == 11 &&
            get_u32(stored, 0x30) == 13,
        "typed store must overlay known words and retain stale raw +0x10");

    UnitMovementUnit linked{};
    linked.id = 0x740;
    effect.active = true;
    effect.linked_unit = &linked;
    require(StoreMapEffectRawRecord(effect, stored.data(), stored.size()) &&
            get_u32(stored, 0x10) == linked.id,
        "a resolved linked unit must publish its current raw pool offset");
    effect.active = false;
    linked.id = 0x780;
    require(StoreMapEffectRawRecord(effect, stored.data(), stored.size()) &&
            get_u32(stored, 0x10) == effect.linked_unit_raw_offset,
        "a free node must publish its preserved raw link without dereferencing it");
}

void test_allocator_release_and_spawn_reuse() {
    using namespace ranker;

    UnitMovementMap map{};
    map.width = 1;
    map.height = 1;
    map.stride_tiles = 1;
    map.cells.resize(1);
    map.cells[0].alternate_flags = 0x20000000u;

    MapEffectContext context{};
    context.map = &map;
    context.callbacks.find_definition = find_definition;
    context.effects.resize(2);
    context.effects[1].id = 1;
    const auto fixture = make_raw_fixture();
    require(HydrateMapEffectRawRecord(
                context.effects[1], fixture.data(), fixture.size()),
        "free fixture hydration must succeed");
    context.free_effect_indices = {1};

    MapEffectInstance* allocated = AllocateMapEffect(context);
    require(allocated == &context.effects[1] && allocated->active,
        "allocator must activate the free-list head");
    require(allocated->effect_id == 0x55 && allocated->flags == 0xa5 &&
            allocated->linked_unit_raw_offset == 0x12345678 &&
            allocated->raw_record == fixture,
        "allocator must retain the complete stale payload");

    ReleaseMapEffect(context, *allocated);
    require(!allocated->active && context.active_effect_indices.empty() &&
            context.free_effect_indices == std::vector<u32>{1},
        "release must only move list membership");
    require(allocated->effect_id == 0x55 && allocated->flags == 0xa5 &&
            allocated->linked_unit_raw_offset == 0x12345678 &&
            allocated->raw_record == fixture,
        "release must retain typed, raw, and unresolved-link payload");

    MapEffectInstance* spawned =
        HandleMapEffectNearestTileSpawn(context, 7, 17, 19);
    require(spawned == allocated && spawned->active &&
            spawned->effect_id == 7 && spawned->flags == 0 &&
            spawned->linked_unit_raw_offset == 0 &&
            spawned->x == 0 && spawned->y == 0 &&
            spawned->frame_timer == 3 && spawned->repeat_count == 5,
        "spawn must overwrite every known live field");
    require(spawned->raw_record == fixture,
        "spawn must retain untyped backing words from the reused slot");

    ReleaseMapEffect(context, *spawned);
    require(!spawned->active && spawned->effect_id == 7 &&
            spawned->frame_timer == 3 && spawned->repeat_count == 5 &&
            spawned->raw_record == fixture,
        "released newly spawned payload must remain available to save");

    UnitMovementUnit linked{};
    linked.id = 0x5a0;
    spawned = HandleMapEffectNearestTileSpawn(context, 7, 0, 0, &linked);
    require(spawned != nullptr && spawned->linked_unit == &linked &&
            spawned->linked_unit_raw_offset == linked.id &&
            (spawned->flags & kMapEffectLinkedFlag) != 0,
        "spawn must keep typed and raw linked-unit mirrors coherent");
    const u32 released_link = spawned->linked_unit_raw_offset;
    ReleaseMapEffect(context, *spawned);
    linked.id = 0x660;
    std::array<u8, kMapEffectRawRecordSize> stored{};
    require(StoreMapEffectRawRecord(*spawned, stored.data(), stored.size()) &&
            get_u32(stored, 0x10) == released_link,
        "release/save must retain raw +0x10 without following a stale host pointer");
}

void test_timer_release_preserves_expired_payload() {
    using namespace ranker;

    MapEffectContext context{};
    context.callbacks.find_definition = find_definition;
    context.frame_counter = 0x20;
    context.effects.resize(2);
    context.effects[1].id = 1;
    const auto fixture = make_raw_fixture();
    require(HydrateMapEffectRawRecord(
                context.effects[1], fixture.data(), fixture.size()),
        "timer fixture hydration must succeed");
    context.effects[1].active = true;
    context.effects[1].effect_id = 7;
    context.effects[1].frame_timer = 1;
    context.effects[1].repeat_count = 1;
    context.active_effect_indices = {1};

    HandleMapEffectTimerTick(context);
    const MapEffectInstance& expired = context.effects[1];
    require(!expired.active && context.active_effect_indices.empty() &&
            context.free_effect_indices == std::vector<u32>{1},
        "timer expiry must move the node to the free list");
    require(expired.effect_id == 7 && expired.frame_timer == 3 &&
            expired.repeat_count == 0 && expired.raw_record == fixture,
        "timer release must retain the final live payload image");
}

void test_product_serialization_paths() {
    const std::string map_effects = read_file(
        "ranker_reconstructed_code/src/ranker_map_effects.cpp");
    const std::string winmain = read_file(
        "ranker_reconstructed_code/src/ranker_winmain.cpp");

    const std::string allocate = function_range(map_effects,
        "MapEffectInstance* AllocateMapEffect", "void ReleaseMapEffect");
    const std::string release = function_range(map_effects,
        "void ReleaseMapEffect", "MapEffectInstance* HandleMapEffectNearestTileSpawn");
    require(allocate.find("MapEffectInstance{}") == std::string::npos &&
            release.find("MapEffectInstance{}") == std::string::npos,
        "allocator/release product paths must not clear payload");

    const std::string sync = function_range(winmain,
        "void sync_default_map_effect_context_session_record()",
        "void stamp_default_gameplay_save_title");
    require(sync.find("for (u32 index = 0; index < materialized_count; ++index)") !=
                std::string::npos &&
            sync.find("StoreMapEffectRawRecord(effect") != std::string::npos,
        "save must publish every materialized physical slot");
    const std::size_t free_begin = sync.find(
        "for (std::size_t position = 0; position < free_indices.size(); ++position)");
    const std::size_t header_begin = sync.find(
        "std::vector<u8>* header = MutableGameplaySessionLoadedRecord(0)",
        free_begin);
    require(free_begin != std::string::npos && header_begin > free_begin,
        "free-list serialization loop must be found");
    const std::string free_loop = sync.substr(free_begin, header_begin - free_begin);
    require(free_loop.find("kGameplayMapEffectObjectTypeOffset") == std::string::npos &&
            free_loop.find("kGameplayMapEffectObjectFlagsOffset") == std::string::npos &&
            free_loop.find("kGameplayMapEffectObjectLinkedUnitOffset") == std::string::npos &&
            free_loop.find("kGameplayMapEffectObjectFrameTimerOffset") == std::string::npos &&
            free_loop.find("kGameplayMapEffectObjectRepeatCountOffset") == std::string::npos,
        "free-list loop must overwrite links only, never payload");

    const std::string restore = function_range(winmain,
        "void initialize_default_map_effect_context_from_session_records()",
        "void initialize_default_unit_effect_runtime_from_session_records");
    const std::size_t hydrate = restore.find("HydrateMapEffectRawRecord(effect");
    const std::size_t active = restore.find("for (u32 index : active_indices)");
    require(hydrate != std::string::npos && active != std::string::npos &&
            hydrate < active,
        "load must hydrate every physical slot before applying membership");
    const std::string active_body = restore.substr(active);
    require(active_body.find("effect.flags |=") == std::string::npos &&
            active_body.find("effect.flags &=") == std::string::npos,
        "load must not derive raw flags from pointer resolution");

    const std::string owner_sync = function_range(winmain,
        "void sync_default_owner_ai_route_object_candidates(",
        "void default_owner_ai_refresh_placement_anchors");
    require(owner_sync.find("effect.linked_unit_raw_offset = assigned_unit_valid") !=
                std::string::npos,
        "owner-AI assignment must update the raw +0x10 mirror");
}

} // namespace

int main() {
    test_raw_record_roundtrip();
    test_allocator_release_and_spawn_reuse();
    test_timer_release_preserves_expired_payload();
    test_product_serialization_paths();
    std::cout << "MAP_EFFECT_PAYLOAD_PRESERVATION_PASS "
                 "pool=retain raw=3c/untyped linked=raw/free "
                 "spawn=known-overwrite save/load=all-slots\n";
    return EXIT_SUCCESS;
}
