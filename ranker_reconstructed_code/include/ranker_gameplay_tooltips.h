#pragma once

#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

struct ProductionOrderCatalog;
struct UnitEquipmentCatalog;

constexpr u32 kGameplayTooltipHandlerCount = 0x40;
constexpr u32 kGameplayTooltipCostCount = 4;
constexpr u32 kGameplayTooltipOwnerCount = 8;
constexpr u32 kGameplayTooltipOwnerUnitTypeCount = 0xaa;
constexpr u32 kGameplayTooltipProductionOrderCount = 0x40;
constexpr u32 kGameplayTooltipInvalidId = 0xffffffffu;
constexpr u32 kGameplayTooltipHoverDelayMs = 1000;
constexpr u32 kGameplayTooltipCostIconTextOffset = 0x10;
constexpr u32 kGameplayTooltipCostTokenTrailingPadding = 4;

constexpr u32 GameplayTooltipCostTokenAdvance(u32 numeric_text_width) {
    return kGameplayTooltipCostIconTextOffset + numeric_text_width +
        kGameplayTooltipCostTokenTrailingPadding;
}

constexpr i32 GameplayTooltipWorldCoordinateFromScreen(i32 camera_coordinate,
    i32 screen_coordinate, u32 screen_extent, u32 world_view_extent) {
    if (screen_extent == 0 || world_view_extent == 0) {
        return camera_coordinate + screen_coordinate;
    }
    return camera_coordinate + static_cast<i32>(
        (static_cast<i64>(screen_coordinate) * world_view_extent) /
        screen_extent);
}

struct GameplayTooltipTextExtent {
    u32 width = 0;
    u32 height = 0;
};

enum class GameplayTooltipDrawCommandKind : u32 {
    filled_box = 0,
    outline_box = 1,
    text = 2,
    icon = 3,
    icon_value = 4,
};

struct GameplayTooltipDrawCommand {
    GameplayTooltipDrawCommandKind kind = GameplayTooltipDrawCommandKind::text;
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
    u8 color = 1;
    u32 icon = 0;
    u32 value = 0;
    std::string text;
};

struct GameplayTooltipCostValues {
    std::array<u32, kGameplayTooltipCostCount> values{};

    bool any() const;
};

struct GameplayTooltipUnitDefinition {
    u32 type = 0;
    std::string name;
    std::string detail_text;
    GameplayTooltipCostValues costs;
    std::vector<u32> prerequisites;
    u32 owner_requirement = kGameplayTooltipInvalidId;
    u32 active_limit = kGameplayTooltipInvalidId;
    bool special_dependency_scope = false;
};

struct GameplayTooltipProductionActionDefinition {
    u32 id = 0;
    std::string name;
    std::string detail_text;
    GameplayTooltipCostValues costs;
    u32 owner_requirement = kGameplayTooltipInvalidId;
    u32 requirement_order_id = kGameplayTooltipInvalidId;
    u32 active_limit = kGameplayTooltipInvalidId;
    u32 queued_limit = kGameplayTooltipInvalidId;
    u32 resource_primary = 0;
    u32 resource_secondary = 0;
};

struct GameplayTooltipProductionOrderDefinition {
    u32 id = 0;
    std::string name;
    std::string detail_text;
    GameplayTooltipCostValues costs;
    std::vector<u32> prerequisites;
    u32 variant_count = 1;
};

struct GameplayTooltipEquipmentDefinition {
    u32 id = 0;
    std::string name;
    std::string detail_text;
    GameplayTooltipCostValues costs;
};

struct GameplayTooltipSelectedUnitState {
    u32 offset = 0;
    u32 type = 0;
    u32 owner = 0;
    u32 area_marker_flags = 0;
    u32 meat_amount = 0;
    std::array<u32, 6> equipment_slots{};
};

inline GameplayTooltipSelectedUnitState BuildGameplayTooltipSelectedUnitState(
    const UnitMovementUnit& unit) {
    GameplayTooltipSelectedUnitState state{};
    state.offset = unit.id;
    state.type = unit.type_id;
    state.owner = unit.owner_id;
    state.area_marker_flags = unit.area_marker_flags;
    // Original selected-unit raw +0x2c is both the carried-meat amount and
    // the source for object 0x1ad's four tooltip tiers.  Experience lives at
    // raw +0x50 and must not influence this tooltip.
    state.meat_amount = unit.action_mode;
    state.equipment_slots = unit.equipment_slots;
    return state;
}

constexpr u32 GameplayTooltipMeatTierForAmount(u32 meat_amount) {
    if (meat_amount == 0) {
        return 0;
    }
    if (meat_amount > 1000) {
        return 4;
    }
    if (meat_amount > 500) {
        return 3;
    }
    if (meat_amount > 100) {
        return 2;
    }
    return 1;
}

inline std::string BuildGameplayMapEffectTooltipText(
    std::string name, u32 effect_id, u32 amount) {
    // FUN_004df316 appends "=" and raw map-effect +0x30 only for IDs 0..4.
    // That raw word is the remaining meat/resource amount for ground drops.
    if (effect_id < 5) {
        name.push_back('=');
        name += std::to_string(amount);
    }
    return name;
}

struct GameplayTooltipState;

using GameplayTooltipRenderCallback = void (*)(GameplayTooltipState& state);
using GameplayTooltipMeasureTextCallback =
    GameplayTooltipTextExtent (*)(GameplayTooltipState& state, const char* text);
using GameplayTooltipDrawTextCallback = void (*)(
    GameplayTooltipState& state, const char* text, i32 x, i32 y, u8 color);
using GameplayTooltipDrawRectCallback =
    void (*)(GameplayTooltipState& state, i32 left, i32 top, i32 right, i32 bottom);
using GameplayTooltipDrawIconCallback =
    void (*)(GameplayTooltipState& state, u32 icon, i32 x, i32 y);
using GameplayTooltipProductionCostCallback = GameplayTooltipCostValues (*)(
    GameplayTooltipState& state, u32 order_id, u32 variant);

struct GameplayTooltipCallbacks {
    GameplayTooltipMeasureTextCallback measure_text = nullptr;
    GameplayTooltipDrawTextCallback draw_text = nullptr;
    GameplayTooltipDrawRectCallback fill_box = nullptr;
    GameplayTooltipDrawRectCallback outline_box = nullptr;
    GameplayTooltipDrawIconCallback draw_icon = nullptr;
    GameplayTooltipProductionCostCallback production_order_costs = nullptr;
};

struct GameplayTooltipState {
    GameplayTooltipCallbacks callbacks;
    std::array<GameplayTooltipRenderCallback, kGameplayTooltipHandlerCount> handlers{};
    std::vector<GameplayTooltipDrawCommand> draw_commands;

    u32 screen_width = 800;
    u32 screen_height = 600;
    u32 world_view_width = 800;
    u32 world_view_height = 600;
    u32 current_tick_ms = 0;
    u32 hover_start_tick_ms = 0;
    u32 scheduled_mode = 0;
    i32 cursor_x = 0;
    i32 cursor_y = 0;
    i32 last_box_left = 0;
    i32 last_box_top = 0;
    i32 last_box_right = 0;
    i32 last_box_bottom = 0;
    // The tooltip renderers at 004de8a0/004de98d select font slot 4.
    u8 font_index = 4;
    u8 text_color = 1;
    u8 requirement_color = 0x41;
    bool emit_backbuffer_draws = false;

    u32 current_unit_type = 0;
    u32 current_object_id = 0;
    u32 current_payload = 0;
    u32 current_production_order_packed = 0;
    u32 hover_flags = 0;
    u32 local_owner = 0;
    u32 map_width_tiles = 0;
    u32 map_height_tiles = 0;
    i32 camera_x = 0;
    i32 camera_y = 0;

    std::string current_text;
    std::string secondary_text;
    std::string requirement_text;
    std::string cost_row_text;
    std::vector<std::string> multiline_text;
    GameplayTooltipCostValues current_costs;
    std::array<u32, kGameplayTooltipCostCount> cost_icons{0, 1, 2, 3};
    std::vector<std::string> platform_text_rows;
    std::vector<std::string> object_texts;
    std::vector<u32> indexed_values;
    std::vector<u32> indexed_amounts;
    std::vector<u32> terrain_flags;

    std::vector<GameplayTooltipUnitDefinition> unit_definitions;
    std::vector<GameplayTooltipProductionActionDefinition> production_actions;
    std::vector<GameplayTooltipProductionOrderDefinition> production_orders;
    std::vector<GameplayTooltipEquipmentDefinition> equipment_definitions;
    GameplayTooltipSelectedUnitState selected_unit;
    bool selected_unit_valid = false;

    std::array<std::array<u32, kGameplayTooltipOwnerUnitTypeCount>,
        kGameplayTooltipOwnerCount> owner_unit_counts{};
    std::array<std::array<u8, kGameplayTooltipProductionOrderCount>,
        kGameplayTooltipOwnerCount> production_order_variants{};
};

inline u32 GameplayTooltipTerrainResourceAmount(
    const GameplayTooltipState& state) {
    if (state.map_width_tiles == 0 || state.map_height_tiles == 0 ||
        state.terrain_flags.empty()) {
        return state.current_payload;
    }

    const i32 world_x = GameplayTooltipWorldCoordinateFromScreen(
        state.camera_x, state.cursor_x, state.screen_width,
        state.world_view_width);
    const i32 world_y = GameplayTooltipWorldCoordinateFromScreen(
        state.camera_y, state.cursor_y, state.screen_height,
        state.world_view_height);
    if (world_x < 0 || world_y < 0) {
        return state.current_payload;
    }

    u32 tile_x = static_cast<u32>(world_x) >> 5;
    const u32 tile_y = static_cast<u32>(world_y) >> 5;
    if (tile_x >= state.map_width_tiles || tile_y >= state.map_height_tiles) {
        return state.current_payload;
    }

    std::size_t index =
        static_cast<std::size_t>(tile_y) * state.map_width_tiles + tile_x;
    if (index >= state.terrain_flags.size()) {
        return state.current_payload;
    }

    if ((state.terrain_flags[index] & 0x800u) != 0 && tile_x != 0) {
        --tile_x;
        index = static_cast<std::size_t>(tile_y) * state.map_width_tiles + tile_x;
        if (index >= state.terrain_flags.size()) {
            return state.current_payload;
        }
    }

    return (state.terrain_flags[index] & 0x0ffff000u) >> 12;
}

GameplayTooltipState& gameplay_tooltip_state();
void InstallDefaultGameplayTooltipHandlers(GameplayTooltipState& state);
void ResetGameplayTooltipDrawCommands(GameplayTooltipState& state);
void LoadGameplayTooltipEquipmentDefinitionsFromCatalog(GameplayTooltipState& state,
    const UnitEquipmentCatalog& catalog);
void LoadGameplayTooltipProductionOrderDefinitionsFromCatalog(GameplayTooltipState& state,
    const ProductionOrderCatalog& catalog);

void ScheduleGameplayTooltip(GameplayTooltipState& state, u32 mode, i32 x, i32 y);
void ScheduleGameplayTooltipImmediate(GameplayTooltipState& state, u32 mode, i32 x, i32 y);
void RenderScheduledGameplayTooltip(GameplayTooltipState& state);

void DrawGameplayTooltipTextBox(GameplayTooltipState& state);
void DrawGameplayTooltipTextBox(GameplayTooltipState& state, const char* text);
void DrawGameplayTooltipMultilineBox(GameplayTooltipState& state);
void DrawGameplayTooltipMultilineBox(GameplayTooltipState& state,
    const std::vector<std::string>& lines);
void DrawGameplayTooltipCostBox(GameplayTooltipState& state);
void DrawGameplayTooltipRequirementBox(GameplayTooltipState& state);

void BuildUnitDefinitionTooltip(GameplayTooltipState& state);
void BuildUnitDefinitionTooltip(GameplayTooltipState& state, u32 unit_type);
void BuildProductionOrderCostTooltip(GameplayTooltipState& state);
void BuildProductionOrderCostTooltip(GameplayTooltipState& state, u32 packed_order);
void BuildSelectedUnitDefinitionTooltip(GameplayTooltipState& state);
void BuildUnitDefinitionCostTooltip(GameplayTooltipState& state);
void BuildUnitDefinitionCostTooltip(GameplayTooltipState& state, u32 unit_type);
void BuildUnitOrProductionCostTooltip(GameplayTooltipState& state);
void BuildSimpleTooltipFromCurrentText(GameplayTooltipState& state);
void BuildIndexedValueTooltip(GameplayTooltipState& state);
void BuildSimpleTooltipVariant0(GameplayTooltipState& state);
void BuildSimpleTooltipVariant1(GameplayTooltipState& state);
void BuildSimpleTooltipVariant2(GameplayTooltipState& state);
void BuildSimpleTooltipVariant3(GameplayTooltipState& state);
void BuildSimpleTooltipVariant4(GameplayTooltipState& state);
void BuildUnitOrObjectTooltip(GameplayTooltipState& state);
void BuildProductionActionTooltip(GameplayTooltipState& state);
void BuildSimpleProductionTooltip(GameplayTooltipState& state);
void BuildSelectedUnitEquipmentSlotTooltip(GameplayTooltipState& state);
void BuildEquipmentDefinitionCostTooltip(GameplayTooltipState& state);
void BuildSimpleEquipmentTooltip(GameplayTooltipState& state);
void BuildNumberedSimpleTooltip(GameplayTooltipState& state);
void BuildMultilineTooltip(GameplayTooltipState& state);
void BuildProductionOrderTooltip(GameplayTooltipState& state);

}
