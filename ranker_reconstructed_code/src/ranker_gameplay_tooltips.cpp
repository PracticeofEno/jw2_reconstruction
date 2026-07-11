#include "ranker_gameplay_tooltips.h"

#include "ranker_production_orders.h"
#include "ranker_sprite_renderer.h"
#include "ranker_text_renderer.h"
#include "ranker_ui_screen.h"
#include "ranker_unit_equipment.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

namespace ranker {
namespace {

GameplayTooltipState g_gameplay_tooltip_state;

constexpr u32 kHoverFlagRequirements = 0x02;
constexpr u32 kHoverFlagCost2 = 0x04;
constexpr u32 kHoverFlagCost1 = 0x10;
constexpr u32 kHoverFlagMissingOwner = 0x20;
constexpr u32 kSimpleBoxXPadding = 10;
constexpr u32 kSimpleBoxYPadding = 3;
constexpr u32 kTextInsetX = 5;
constexpr u32 kTextInsetY = 3;
constexpr u32 kCostIconSpacing = 0x14;
constexpr u32 kCostIconTextOffset = 0x10;

bool has_text(const std::string& text) {
    return !text.empty();
}

std::string fallback_label(const char* prefix, u32 id) {
    std::ostringstream out;
    out << prefix << ' ' << id;
    return out.str();
}

std::string unsigned_text(u32 value) {
    return std::to_string(value);
}

GameplayTooltipTextExtent fallback_measure(const char* text) {
    GameplayTooltipTextExtent extent{};
    if (text == nullptr || text[0] == '\0') {
        return extent;
    }
    extent.width = static_cast<u32>(std::strlen(text) * 8);
    extent.height = 16;
    return extent;
}

GameplayTooltipTextExtent measure_text(GameplayTooltipState& state, const char* text) {
    if (state.callbacks.measure_text != nullptr) {
        return state.callbacks.measure_text(state, text);
    }
    if (text == nullptr || text[0] == '\0') {
        return {};
    }

    SelectTextMetricFont(state.font_index);
    if (MeasureTextExtent(text)) {
        const TextRendererState& renderer = text_renderer_state();
        if (renderer.measured_width != 0 || renderer.measured_height != 0) {
            return {renderer.measured_width, renderer.measured_height};
        }
    }
    return fallback_measure(text);
}

void append_command(GameplayTooltipState& state, GameplayTooltipDrawCommand command) {
    state.draw_commands.push_back(std::move(command));
}

void emit_fill(GameplayTooltipState& state, i32 left, i32 top, i32 right, i32 bottom) {
    append_command(state, {GameplayTooltipDrawCommandKind::filled_box,
        left, top, right, bottom, 0, 0, 0, {}});
    if (state.callbacks.fill_box != nullptr) {
        state.callbacks.fill_box(state, left, top, right, bottom);
    }
    else if (state.emit_backbuffer_draws) {
        DrawBackBufferFilledRectangle16(left, top, right, bottom);
    }
}

void emit_outline(GameplayTooltipState& state, i32 left, i32 top, i32 right, i32 bottom) {
    append_command(state, {GameplayTooltipDrawCommandKind::outline_box,
        left, top, right, bottom, 0, 0, 0, {}});
    if (state.callbacks.outline_box != nullptr) {
        state.callbacks.outline_box(state, left, top, right, bottom);
    }
    else if (state.emit_backbuffer_draws) {
        DrawBackBufferRectangleOutline16(left, top, right - left, bottom - top);
    }
}

void emit_text(GameplayTooltipState& state, const char* text, i32 x, i32 y, u8 color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    GameplayTooltipDrawCommand command{};
    command.kind = GameplayTooltipDrawCommandKind::text;
    command.left = x;
    command.top = y;
    command.color = color;
    command.text = text;
    append_command(state, command);

    if (state.callbacks.draw_text != nullptr) {
        state.callbacks.draw_text(state, text, x, y, color);
    }
    else if (state.emit_backbuffer_draws) {
        SelectTextDrawFont(state.font_index);
        SetTextCursor(x, y, color);
        DrawTextString(text);
    }
}

void emit_icon(GameplayTooltipState& state, u32 icon, i32 x, i32 y) {
    GameplayTooltipDrawCommand command{};
    command.kind = GameplayTooltipDrawCommandKind::icon;
    command.left = x;
    command.top = y;
    command.icon = icon;
    append_command(state, command);

    if (state.callbacks.draw_icon != nullptr) {
        state.callbacks.draw_icon(state, icon, x, y);
    }
    else if (state.emit_backbuffer_draws) {
        DrawResourceSpriteNormal(icon, x, y);
    }
}

void emit_icon_value(
    GameplayTooltipState& state, u32 icon, u32 value, i32 icon_x, i32 y) {
    emit_icon(state, icon, icon_x, y);
    GameplayTooltipDrawCommand value_command{};
    value_command.kind = GameplayTooltipDrawCommandKind::icon_value;
    value_command.left = icon_x + static_cast<i32>(kCostIconTextOffset);
    value_command.top = y;
    value_command.icon = icon;
    value_command.value = value;
    value_command.text = unsigned_text(value);
    append_command(state, value_command);
    emit_text(state, value_command.text.c_str(), value_command.left, y, state.text_color);
}

GameplayTooltipTextExtent measure_multiline(
    GameplayTooltipState& state, const std::vector<std::string>& lines) {
    GameplayTooltipTextExtent extent{};
    for (const std::string& line : lines) {
        const GameplayTooltipTextExtent line_extent = measure_text(state, line.c_str());
        extent.width = std::max(extent.width, line_extent.width);
        extent.height += line_extent.height;
    }
    return extent;
}

void position_box(GameplayTooltipState& state, u32 content_width, u32 content_height) {
    i32 top = state.cursor_y - static_cast<i32>(content_height) - 5;
    if (top < 0) {
        top = 0;
    }

    i32 left = state.cursor_x;
    const u32 desired_right =
        static_cast<u32>(std::max(0, state.cursor_x)) + content_width + 0x0c;
    if (state.screen_width <= desired_right) {
        left -= static_cast<i32>(desired_right - state.screen_width);
    }
    if (left < 0) {
        left = 0;
    }

    state.last_box_left = left;
    state.last_box_top = top;
    state.last_box_right = left + static_cast<i32>(content_width) + 10;
    state.last_box_bottom = top + static_cast<i32>(content_height) + 3;
}

void draw_box_shell(GameplayTooltipState& state) {
    emit_fill(state, state.last_box_left, state.last_box_top,
        state.last_box_right, state.last_box_bottom);
    emit_outline(state, state.last_box_left, state.last_box_top,
        state.last_box_right, state.last_box_bottom);
}

const GameplayTooltipUnitDefinition* find_unit_definition(
    const GameplayTooltipState& state, u32 unit_type) {
    auto it = std::find_if(state.unit_definitions.begin(), state.unit_definitions.end(),
        [unit_type](const GameplayTooltipUnitDefinition& definition) {
            return definition.type == unit_type;
        });
    return it == state.unit_definitions.end() ? nullptr : &*it;
}

const GameplayTooltipProductionActionDefinition* find_action_definition(
    const GameplayTooltipState& state, u32 action_id) {
    auto it = std::find_if(state.production_actions.begin(), state.production_actions.end(),
        [action_id](const GameplayTooltipProductionActionDefinition& definition) {
            return definition.id == action_id;
        });
    return it == state.production_actions.end() ? nullptr : &*it;
}

const GameplayTooltipProductionOrderDefinition* find_order_definition(
    const GameplayTooltipState& state, u32 order_id) {
    auto it = std::find_if(state.production_orders.begin(), state.production_orders.end(),
        [order_id](const GameplayTooltipProductionOrderDefinition& definition) {
            return definition.id == order_id;
        });
    return it == state.production_orders.end() ? nullptr : &*it;
}

const GameplayTooltipEquipmentDefinition* find_equipment_definition(
    const GameplayTooltipState& state, u32 equipment_id) {
    auto it = std::find_if(state.equipment_definitions.begin(),
        state.equipment_definitions.end(),
        [equipment_id](const GameplayTooltipEquipmentDefinition& definition) {
            return definition.id == equipment_id;
        });
    return it == state.equipment_definitions.end() ? nullptr : &*it;
}

bool owner_has_unit(const GameplayTooltipState& state, u32 owner, u32 unit_type) {
    if (owner >= kGameplayTooltipOwnerCount ||
        unit_type >= kGameplayTooltipOwnerUnitTypeCount) {
        return false;
    }
    return state.owner_unit_counts[owner][unit_type] != 0;
}

bool any_prerequisite_is_satisfied(
    const GameplayTooltipState& state, const std::vector<u32>& prerequisites) {
    for (u32 prerequisite : prerequisites) {
        if (owner_has_unit(state, state.local_owner, prerequisite)) {
            return true;
        }
    }
    return false;
}

std::string requirement_label(
    const GameplayTooltipState& state, const std::vector<u32>& prerequisites,
    bool any_one_prerequisite_satisfies = false,
    const char* separator = ", ") {
    if (any_one_prerequisite_satisfies &&
        any_prerequisite_is_satisfied(state, prerequisites)) {
        return {};
    }

    std::ostringstream out;
    bool appended = false;
    for (u32 prerequisite : prerequisites) {
        if (owner_has_unit(state, state.local_owner, prerequisite)) {
            continue;
        }
        if (appended) {
            out << (separator != nullptr ? separator : ", ");
        }
        if (const GameplayTooltipUnitDefinition* definition =
                find_unit_definition(state, prerequisite)) {
            out << definition->name;
        }
        else {
            out << "Unit " << prerequisite;
        }
        appended = true;
    }
    return out.str();
}

void set_costs(GameplayTooltipState& state, const GameplayTooltipCostValues& costs) {
    state.current_costs = costs;
    state.cost_row_text = costs.any() ? "Cost" : "";
}

void set_unit_costs(GameplayTooltipState& state,
    const GameplayTooltipUnitDefinition* definition, u32 unit_type) {
    if (definition != nullptr) {
        set_costs(state, definition->costs);
    }
    else {
        GameplayTooltipCostValues costs{};
        costs.values[0] = unit_type == 0 ? 0 : unit_type;
        set_costs(state, costs);
    }
}

std::string platform_text_or_fallback(const GameplayTooltipState& state,
    std::size_t row, const std::string& fallback) {
    if (row < state.platform_text_rows.size() &&
        !state.platform_text_rows[row].empty()) {
        return state.platform_text_rows[row];
    }
    return fallback;
}

std::string unit_definition_name_or_fallback(
    const GameplayTooltipState& state, u32 unit_type) {
    if (const GameplayTooltipUnitDefinition* definition =
            find_unit_definition(state, unit_type)) {
        if (has_text(definition->name)) {
            return definition->name;
        }
    }
    return fallback_label("Unit", unit_type);
}

std::string equipment_definition_name_or_fallback(
    const GameplayTooltipState& state, u32 equipment_id) {
    if (const GameplayTooltipEquipmentDefinition* definition =
            find_equipment_definition(state, equipment_id)) {
        if (has_text(definition->name)) {
            return definition->name;
        }
    }
    return fallback_label("Equipment", equipment_id);
}

std::string production_order_name_or_fallback(
    const GameplayTooltipState& state, u32 order_id) {
    if (const GameplayTooltipProductionOrderDefinition* definition =
            find_order_definition(state, order_id)) {
        if (has_text(definition->name)) {
            return definition->name;
        }
    }
    return fallback_label("Order", order_id);
}

void draw_cost_or_simple(GameplayTooltipState& state) {
    if (state.current_costs.any() || has_text(state.secondary_text) ||
        has_text(state.cost_row_text)) {
        DrawGameplayTooltipCostBox(state);
        return;
    }
    DrawGameplayTooltipTextBox(state);
}

u32 low_word(u32 value) {
    return value & 0xffffu;
}

u32 high_word(u32 value) {
    return value >> 16;
}

u32 local_production_order_variant(
    const GameplayTooltipState& state, u32 order_id) {
    if (state.local_owner >= state.production_order_variants.size() ||
        order_id >= state.production_order_variants[state.local_owner].size()) {
        return 0;
    }
    return state.production_order_variants[state.local_owner][order_id];
}

u32 numbered_tooltip_tile_value(const GameplayTooltipState& state) {
    if (state.map_width_tiles == 0 || state.map_height_tiles == 0 ||
        state.terrain_flags.empty()) {
        return state.current_payload;
    }

    const i32 world_x = state.camera_x + state.cursor_x;
    const i32 world_y = state.camera_y + state.cursor_y;
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

GameplayTooltipCostValues production_order_costs(
    GameplayTooltipState& state, u32 order_id, u32 variant) {
    if (state.callbacks.production_order_costs != nullptr) {
        return state.callbacks.production_order_costs(state, order_id, variant);
    }
    if (const GameplayTooltipProductionOrderDefinition* order =
            find_order_definition(state, order_id)) {
        GameplayTooltipCostValues costs = order->costs;
        const u32 effective_variant =
            variant != 0 ? variant : local_production_order_variant(state, order_id);
        if (effective_variant > 1 && costs.values[0] != 0) {
            costs.values[0] *= effective_variant;
        }
        return costs;
    }
    GameplayTooltipCostValues fallback{};
    fallback.values[0] = order_id + 1;
    fallback.values[3] = variant;
    return fallback;
}

void build_requirement_for_unit(
    GameplayTooltipState& state, const GameplayTooltipUnitDefinition* definition) {
    state.requirement_text.clear();
    if (definition != nullptr) {
        const std::string separator =
            definition->special_dependency_scope ?
            platform_text_or_fallback(state, 86, " or ") :
            std::string{", "};
        state.requirement_text = requirement_label(
            state, definition->prerequisites, definition->special_dependency_scope,
            separator.c_str());
        if (definition->owner_requirement != kGameplayTooltipInvalidId &&
            definition->owner_requirement != state.local_owner) {
            if (!state.requirement_text.empty()) {
                state.requirement_text += ", ";
            }
            state.requirement_text += "Owner";
        }
        if (definition->active_limit == 0) {
            if (!state.requirement_text.empty()) {
                state.requirement_text += ", ";
            }
            state.requirement_text += "Limit";
        }
    }
    if (definition == nullptr && state.requirement_text.empty()) {
        state.requirement_text = "Requirement";
    }
}

void build_unit_definition(GameplayTooltipState& state, u32 unit_type, bool force_cost_only) {
    const GameplayTooltipUnitDefinition* definition = find_unit_definition(state, unit_type);
    state.current_text =
        definition != nullptr && has_text(definition->name) ?
        definition->name : fallback_label("Unit", unit_type);
    state.secondary_text.clear();

    if (!force_cost_only && (state.hover_flags & kHoverFlagRequirements) != 0) {
        state.current_text += platform_text_or_fallback(state, 84, " Requirement:");
        build_requirement_for_unit(state, definition);
        DrawGameplayTooltipRequirementBox(state);
        return;
    }

    set_unit_costs(state, definition, unit_type);
    state.cost_row_text.clear();
    DrawGameplayTooltipCostBox(state);
}

void draw_simple_variant(GameplayTooltipState& state) {
    DrawGameplayTooltipTextBox(state);
}

u32 selected_equipment_value(const GameplayTooltipState& state) {
    if (!state.selected_unit_valid) {
        return 0;
    }
    switch (state.current_object_id) {
    case 0x1ad: {
        const u32 value = state.selected_unit.tier_value;
        if (value == 0) {
            return 0;
        }
        if (value > 1000) {
            return 4;
        }
        if (value > 500) {
            return 3;
        }
        if (value > 100) {
            return 2;
        }
        return 1;
    }
    case 0x1ae:
        return state.selected_unit.equipment_slots[4];
    case 0x1af:
        return state.selected_unit.equipment_slots[5];
    case 0x1b0:
        return state.selected_unit.equipment_slots[0];
    case 0x1b1:
        return state.selected_unit.equipment_slots[1];
    case 0x1b2:
        return state.selected_unit.equipment_slots[2];
    default:
        return state.selected_unit.equipment_slots[3];
    }
}

} // namespace

bool GameplayTooltipCostValues::any() const {
    return std::any_of(values.begin(), values.end(),
        [](u32 value) { return value != 0; });
}

GameplayTooltipState& gameplay_tooltip_state() {
    InstallDefaultGameplayTooltipHandlers(g_gameplay_tooltip_state);
    return g_gameplay_tooltip_state;
}

void InstallDefaultGameplayTooltipHandlers(GameplayTooltipState& state) {
    if (state.handlers[0x02] != nullptr) {
        return;
    }
    state.handlers[0x02] = BuildUnitOrProductionCostTooltip;
    state.handlers[0x03] = BuildSimpleTooltipFromCurrentText;
    state.handlers[0x04] = BuildSimpleTooltipVariant0;
    // Original dispatch entry 5 jumps through 00401f19 to the no-op RET at
    // 004df3b2.  Keeping this slot empty also prevents interface-mask hover
    // from drawing the selected unit definition name over the world/HUD.
    state.handlers[0x05] = nullptr;
    state.handlers[0x09] = BuildIndexedValueTooltip;
    state.handlers[0x0b] = BuildSimpleEquipmentTooltip;
    state.handlers[0x0c] = BuildNumberedSimpleTooltip;
    state.handlers[0x0d] = BuildUnitOrObjectTooltip;
    state.handlers[0x0e] = BuildProductionActionTooltip;
    state.handlers[0x0f] = BuildSimpleProductionTooltip;
    state.handlers[0x10] = BuildSimpleTooltipVariant1;
    state.handlers[0x11] = BuildMultilineTooltip;
    state.handlers[0x12] = BuildProductionOrderTooltip;
    state.handlers[0x13] = BuildEquipmentDefinitionCostTooltip;
    state.handlers[0x14] = BuildSelectedUnitEquipmentSlotTooltip;
}

void ResetGameplayTooltipDrawCommands(GameplayTooltipState& state) {
    state.draw_commands.clear();
}

void LoadGameplayTooltipEquipmentDefinitionsFromCatalog(GameplayTooltipState& state,
    const UnitEquipmentCatalog& catalog) {
    state.equipment_definitions.clear();
    state.equipment_definitions.reserve(catalog.effects.size());
    for (const UnitEquipmentEffectDefinition& effect : catalog.effects) {
        GameplayTooltipEquipmentDefinition definition{};
        definition.id = effect.id;
        definition.name = effect.display_name;
        definition.detail_text = effect.detail_text;
        definition.costs.values[0] = effect.tooltip_primary_cost;
        definition.costs.values[1] = effect.tooltip_secondary_cost;
        state.equipment_definitions.push_back(std::move(definition));
    }
}

void LoadGameplayTooltipProductionOrderDefinitionsFromCatalog(GameplayTooltipState& state,
    const ProductionOrderCatalog& catalog) {
    state.production_orders.clear();
    state.production_orders.reserve(catalog.definitions.size());
    for (const ProductionOrderDefinition& order : catalog.definitions) {
        GameplayTooltipProductionOrderDefinition definition{};
        definition.id = order.id;
        definition.name = order.display_name;
        definition.detail_text = order.detail_text;
        definition.costs.values[0] = CalculateProductionOrderCost(order.primary_cost, 0);
        definition.costs.values[1] = CalculateProductionOrderCost(order.secondary_cost, 0);
        definition.prerequisites = order.prerequisite_type_ids;
        definition.variant_count = order.max_variant_count;
        state.production_orders.push_back(std::move(definition));
    }
}

void ScheduleGameplayTooltip(GameplayTooltipState& state, u32 mode, i32 x, i32 y) {
    state.cursor_x = x;
    state.cursor_y = y;
    if (state.scheduled_mode != mode) {
        state.hover_start_tick_ms = state.current_tick_ms;
        state.scheduled_mode = mode;
    }
}

void ScheduleGameplayTooltipImmediate(
    GameplayTooltipState& state, u32 mode, i32 x, i32 y) {
    state.cursor_x = x;
    state.cursor_y = y;
    state.scheduled_mode = mode;
    state.hover_start_tick_ms = state.current_tick_ms - 0x3f2u;
}

void RenderScheduledGameplayTooltip(GameplayTooltipState& state) {
    InstallDefaultGameplayTooltipHandlers(state);
    state.current_tick_ms = RefreshLegacyTickTime();
    if (state.current_tick_ms - state.hover_start_tick_ms < kGameplayTooltipHoverDelayMs) {
        return;
    }
    if (state.scheduled_mode >= state.handlers.size()) {
        return;
    }
    if (GameplayTooltipRenderCallback handler = state.handlers[state.scheduled_mode]) {
        handler(state);
    }
}

void DrawGameplayTooltipTextBox(GameplayTooltipState& state) {
    DrawGameplayTooltipTextBox(state, state.current_text.c_str());
}

void DrawGameplayTooltipTextBox(GameplayTooltipState& state, const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    SelectTextDrawFont(state.font_index);
    SelectTextMetricFont(state.font_index);
    const GameplayTooltipTextExtent extent = measure_text(state, text);
    position_box(state, extent.width, extent.height);
    draw_box_shell(state);
    emit_text(state, text, state.last_box_left + static_cast<i32>(kTextInsetX),
        state.last_box_top + static_cast<i32>(kTextInsetY), state.text_color);
}

void DrawGameplayTooltipMultilineBox(GameplayTooltipState& state) {
    DrawGameplayTooltipMultilineBox(state, state.multiline_text);
}

void DrawGameplayTooltipMultilineBox(
    GameplayTooltipState& state, const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return;
    }

    SelectTextDrawFont(state.font_index);
    SelectTextMetricFont(state.font_index);
    const GameplayTooltipTextExtent extent = measure_multiline(state, lines);
    position_box(state, extent.width, extent.height);
    draw_box_shell(state);

    i32 y = state.last_box_top + static_cast<i32>(kTextInsetY);
    const i32 x = state.last_box_left + static_cast<i32>(kTextInsetX);
    for (const std::string& line : lines) {
        const GameplayTooltipTextExtent line_extent = measure_text(state, line.c_str());
        emit_text(state, line.c_str(), x, y, state.text_color);
        y += static_cast<i32>(line_extent.height);
    }
}

void DrawGameplayTooltipCostBox(GameplayTooltipState& state) {
    if (state.current_text.empty()) {
        return;
    }

    SelectTextDrawFont(state.font_index);
    SelectTextMetricFont(state.font_index);

    GameplayTooltipTextExtent primary = measure_text(state, state.current_text.c_str());
    u32 width = primary.width;
    u32 height = primary.height + 3;
    const u32 secondary_advance = primary.height + 3;
    u32 cost_row_advance = primary.height + 5;
    if (!state.secondary_text.empty()) {
        const GameplayTooltipTextExtent secondary =
            measure_text(state, state.secondary_text.c_str());
        width = std::max(width, secondary.width);
        cost_row_advance = secondary.height + 5;
        height += secondary.height + 3;
    }

    const bool has_cost_row =
        state.current_costs.any() || has_text(state.cost_row_text);
    if (has_cost_row) {
        const GameplayTooltipTextExtent row_extent =
            measure_text(state, state.cost_row_text.c_str());
        u32 row_width = row_extent.width;
        for (u32 cost : state.current_costs.values) {
            if (cost != 0) {
                const std::string value_text = unsigned_text(cost);
                row_width += kCostIconTextOffset +
                    measure_text(state, value_text.c_str()).width + 4;
            }
        }
        width = std::max(width, row_width);
        height += std::max<u32>(row_extent.height + 3, 0x10);
    }

    position_box(state, width, height);
    draw_box_shell(state);

    i32 x = state.last_box_left + static_cast<i32>(kTextInsetX);
    i32 y = state.last_box_top + static_cast<i32>(kTextInsetY);
    emit_text(state, state.current_text.c_str(), x, y, state.text_color);
    if (!state.secondary_text.empty()) {
        y += static_cast<i32>(secondary_advance);
        emit_text(state, state.secondary_text.c_str(), x, y, state.text_color);
    }
    if (has_cost_row) {
        y += static_cast<i32>(cost_row_advance);
        if (!state.cost_row_text.empty()) {
            emit_text(state, state.cost_row_text.c_str(), x, y, state.text_color);
            x += static_cast<i32>(measure_text(state, state.cost_row_text.c_str()).width);
        }
        for (u32 i = 0; i < state.current_costs.values.size(); ++i) {
            const u32 value = state.current_costs.values[i];
            if (value == 0) {
                continue;
            }
            const std::string value_text = unsigned_text(value);
            emit_icon_value(state, state.cost_icons[i], value, x, y);
            x += static_cast<i32>(kCostIconTextOffset +
                measure_text(state, value_text.c_str()).width + 4);
        }
    }
}

void DrawGameplayTooltipRequirementBox(GameplayTooltipState& state) {
    if (state.current_text.empty()) {
        return;
    }

    SelectTextDrawFont(state.font_index);
    SelectTextMetricFont(state.font_index);
    GameplayTooltipTextExtent primary = measure_text(state, state.current_text.c_str());
    u32 width = primary.width;
    u32 height = primary.height;
    const u32 second_line_advance = primary.height + 2;

    if (!state.requirement_text.empty()) {
        const GameplayTooltipTextExtent requirement =
            measure_text(state, state.requirement_text.c_str());
        width = std::max(width, requirement.width);
        height += requirement.height + 2;
    }

    position_box(state, width, height);
    draw_box_shell(state);
    const i32 x = state.last_box_left + static_cast<i32>(kTextInsetX);
    i32 y = state.last_box_top + static_cast<i32>(kTextInsetY);
    emit_text(state, state.current_text.c_str(), x, y, state.text_color);
    if (!state.requirement_text.empty()) {
        y += static_cast<i32>(second_line_advance);
        emit_text(state, state.requirement_text.c_str(), x, y, state.requirement_color);
    }
}

void BuildUnitDefinitionTooltip(GameplayTooltipState& state) {
    BuildUnitDefinitionTooltip(state, state.current_unit_type);
}

void BuildUnitDefinitionTooltip(GameplayTooltipState& state, u32 unit_type) {
    build_unit_definition(state, unit_type, false);
}

void BuildProductionOrderCostTooltip(GameplayTooltipState& state) {
    BuildProductionOrderCostTooltip(state, state.current_production_order_packed);
}

void BuildProductionOrderCostTooltip(GameplayTooltipState& state, u32 packed_order) {
    const u32 display_variant = std::max<u32>(1, low_word(packed_order));
    const u32 variant = display_variant - 1;
    const u32 order_id = high_word(packed_order);
    const GameplayTooltipProductionOrderDefinition* order =
        find_order_definition(state, order_id);
    state.current_text =
        order != nullptr && has_text(order->name) ?
        order->name : fallback_label("Order", order_id);
    state.secondary_text.clear();
    state.current_costs = production_order_costs(state, order_id, variant);
    state.cost_row_text.clear();
    DrawGameplayTooltipCostBox(state);
}

void BuildSelectedUnitDefinitionTooltip(GameplayTooltipState& state) {
    const u32 unit_type =
        state.current_object_id != 0 ? state.current_object_id :
        state.selected_unit.type;
    build_unit_definition(state, unit_type, false);
}

void BuildUnitDefinitionCostTooltip(GameplayTooltipState& state) {
    BuildUnitDefinitionCostTooltip(state, state.current_unit_type);
}

void BuildUnitDefinitionCostTooltip(GameplayTooltipState& state, u32 unit_type) {
    build_unit_definition(state, unit_type, true);
}

void BuildUnitOrProductionCostTooltip(GameplayTooltipState& state) {
    if (state.current_production_order_packed == 0) {
        BuildUnitDefinitionTooltip(state);
        return;
    }
    BuildProductionOrderCostTooltip(state);
}

void BuildSimpleTooltipFromCurrentText(GameplayTooltipState& state) {
    const u32 payload = state.current_payload;
    const std::size_t row = payload < 3 ? 69u + payload : 196u + payload - 3u;
    state.current_text =
        platform_text_or_fallback(state, row, state.current_text);
    DrawGameplayTooltipTextBox(state);
}

void BuildIndexedValueTooltip(GameplayTooltipState& state) {
    if (state.current_payload < state.indexed_values.size() &&
        state.indexed_values[state.current_payload] < 5) {
        const u32 value = state.indexed_values[state.current_payload];
        const u32 amount = state.current_payload < state.indexed_amounts.size() ?
            state.indexed_amounts[state.current_payload] :
            value;
        const GameplayTooltipEquipmentDefinition* equipment =
            find_equipment_definition(state, value);
        if (equipment != nullptr && has_text(equipment->name)) {
            state.current_text = equipment->name;
        }
        std::ostringstream text;
        text << state.current_text << '=' << amount;
        state.current_text = text.str();
    }
    DrawGameplayTooltipTextBox(state);
}

void BuildSimpleTooltipVariant0(GameplayTooltipState& state) {
    state.current_text = platform_text_or_fallback(
        state, 77u + state.current_payload, state.current_text);
    draw_simple_variant(state);
}

void BuildSimpleTooltipVariant1(GameplayTooltipState& state) {
    const std::size_t row =
        (state.selected_unit.area_marker_flags & 0x80000000u) != 0 ? 88u : 87u;
    state.current_text = platform_text_or_fallback(state, row, state.current_text);
    draw_simple_variant(state);
}

void BuildSimpleTooltipVariant2(GameplayTooltipState& state) {
    const u32 unit_type = state.selected_unit_valid ?
        state.selected_unit.type : state.current_unit_type;
    state.current_text = unit_definition_name_or_fallback(state, unit_type);
    draw_simple_variant(state);
}

void BuildSimpleTooltipVariant3(GameplayTooltipState& state) {
    const u32 unit_type = state.selected_unit_valid ?
        state.selected_unit.type : state.current_unit_type;
    state.current_text = unit_definition_name_or_fallback(state, unit_type);
    draw_simple_variant(state);
}

void BuildSimpleTooltipVariant4(GameplayTooltipState& state) {
    const u32 unit_type = state.selected_unit_valid ?
        state.selected_unit.type : state.current_unit_type;
    state.current_text = unit_definition_name_or_fallback(state, unit_type);
    draw_simple_variant(state);
}

void BuildUnitOrObjectTooltip(GameplayTooltipState& state) {
    const u32 object_id = state.current_object_id;
    if (object_id >= 0x134 && object_id < 0x194) {
        const std::string prefix =
            platform_text_or_fallback(state, 162, state.current_text);
        const std::string unit_name =
            unit_definition_name_or_fallback(state, object_id - 0x134);
        if (!prefix.empty()) {
            state.current_text = prefix + " " + unit_name;
        } else {
            state.current_text = unit_name;
        }
        DrawGameplayTooltipTextBox(state);
        return;
    }

    if (object_id == 0xc8) {
        return;
    }

    if (object_id == 0xb5 &&
        (state.hover_flags & kHoverFlagRequirements) != 0) {
        BuildUnitDefinitionTooltip(state, state.current_unit_type);
        return;
    }
    if (object_id == 0xb5) {
        const std::string prefix =
            platform_text_or_fallback(state, 93, state.current_text);
        const std::string unit_name =
            unit_definition_name_or_fallback(state, state.current_unit_type);
        state.current_text = prefix + unit_name;
        DrawGameplayTooltipTextBox(state);
        return;
    }

    if (object_id == 0xbb &&
        (state.hover_flags & kHoverFlagRequirements) != 0) {
        if (object_id < state.object_texts.size() &&
            !state.object_texts[object_id].empty()) {
            state.current_text = state.object_texts[object_id];
        }
        else if (state.current_text.empty()) {
            state.current_text = fallback_label("Object", object_id);
        }
        state.requirement_text = production_order_name_or_fallback(state, 42);
        DrawGameplayTooltipRequirementBox(state);
        return;
    }

    if (object_id >= 0xbc && object_id <= 0xbf) {
        if (object_id < state.object_texts.size() &&
            !state.object_texts[object_id].empty()) {
            state.current_text = state.object_texts[object_id];
        }
        else if (state.current_text.empty()) {
            state.current_text = fallback_label("Object", object_id);
        }

        if ((state.hover_flags & kHoverFlagRequirements) != 0) {
            const u32 action_id = object_id - 0x9c;
            const GameplayTooltipProductionActionDefinition* action =
                find_action_definition(state, action_id);
            if (action != nullptr &&
                action->requirement_order_id != kGameplayTooltipInvalidId) {
                state.requirement_text = production_order_name_or_fallback(
                    state, action->requirement_order_id);
                DrawGameplayTooltipRequirementBox(state);
                return;
            }
            DrawGameplayTooltipTextBox(state);
            return;
        }

        if ((state.hover_flags & kHoverFlagMissingOwner) != 0) {
            state.requirement_text =
                platform_text_or_fallback(state, 188, "Requirement");
            DrawGameplayTooltipRequirementBox(state);
            return;
        }

        DrawGameplayTooltipTextBox(state);
        return;
    }

    if (object_id >= 0xaa) {
        if (object_id < state.object_texts.size() && !state.object_texts[object_id].empty()) {
            state.current_text = state.object_texts[object_id];
        }
        else if (state.current_text.empty()) {
            state.current_text = fallback_label("Object", object_id);
        }
        DrawGameplayTooltipTextBox(state);
        return;
    }

    build_unit_definition(state, object_id, false);
}

void BuildProductionActionTooltip(GameplayTooltipState& state) {
    const u32 action_id = state.current_object_id >= 0xd4 ?
        state.current_object_id - 0xd4 : state.current_object_id;
    const GameplayTooltipProductionActionDefinition* action =
        find_action_definition(state, action_id);
    state.current_text =
        action != nullptr && has_text(action->name) ?
        action->name : fallback_label("Action", action_id);
    state.secondary_text.clear();

    if ((state.hover_flags & kHoverFlagCost2) != 0 && action != nullptr) {
        GameplayTooltipCostValues costs{};
        costs.values[2] = action->resource_secondary;
        state.current_costs = costs;
        state.secondary_text = platform_text_or_fallback(state, 112, "");
        state.cost_row_text.clear();
        DrawGameplayTooltipCostBox(state);
        return;
    }
    if ((state.hover_flags & kHoverFlagCost1) != 0 && action != nullptr) {
        GameplayTooltipCostValues costs{};
        costs.values[1] = action->resource_primary;
        state.current_costs = costs;
        state.secondary_text = platform_text_or_fallback(state, 122, "");
        state.cost_row_text.clear();
        DrawGameplayTooltipCostBox(state);
        return;
    }
    if ((state.hover_flags & kHoverFlagRequirements) != 0 && action != nullptr) {
        if (action->requirement_order_id != kGameplayTooltipInvalidId) {
            state.requirement_text = production_order_name_or_fallback(
                state, action->requirement_order_id);
            DrawGameplayTooltipRequirementBox(state);
            return;
        }
        if (action->active_limit != kGameplayTooltipInvalidId &&
            action->active_limit > 0) {
            state.requirement_text =
                platform_text_or_fallback(state, 113, "") +
                std::to_string(action->active_limit + 1);
            DrawGameplayTooltipRequirementBox(state);
            return;
        }
    }

    if (action == nullptr) {
        state.current_costs = {};
        state.cost_row_text.clear();
        DrawGameplayTooltipTextBox(state);
        return;
    }

    if (action->resource_secondary != 0) {
        GameplayTooltipCostValues costs{};
        costs.values[2] = action->resource_secondary;
        set_costs(state, costs);
        state.cost_row_text.clear();
        DrawGameplayTooltipCostBox(state);
        return;
    }
    if (action->resource_primary != 0) {
        GameplayTooltipCostValues costs{};
        costs.values[1] = action->resource_primary;
        set_costs(state, costs);
        state.cost_row_text.clear();
        DrawGameplayTooltipCostBox(state);
        return;
    }

    state.current_costs = {};
    state.cost_row_text.clear();
    DrawGameplayTooltipTextBox(state);
}

void BuildSimpleProductionTooltip(GameplayTooltipState& state) {
    const u32 equipment_id = state.current_object_id >= 0x1b4 ?
        state.current_object_id - 0x1b4 : state.current_object_id;
    state.current_text = equipment_definition_name_or_fallback(state, equipment_id);
    DrawGameplayTooltipTextBox(state);
}

void BuildSelectedUnitEquipmentSlotTooltip(GameplayTooltipState& state) {
    const u32 value = selected_equipment_value(state);
    if (value == 0) {
        return;
    }
    if (state.current_text.empty()) {
        state.current_text = equipment_definition_name_or_fallback(state, value);
    }
    DrawGameplayTooltipTextBox(state);
}

void BuildEquipmentDefinitionCostTooltip(GameplayTooltipState& state) {
    const u32 equipment_id = state.current_object_id >= 0x24a ?
        state.current_object_id - 0x24a : state.current_object_id;
    const GameplayTooltipEquipmentDefinition* equipment =
        find_equipment_definition(state, equipment_id);
    state.current_text =
        equipment != nullptr && has_text(equipment->name) ?
        equipment->name : fallback_label("Equipment", equipment_id);
    state.secondary_text.clear();
    if (equipment != nullptr) {
        state.current_costs = equipment->costs;
    }
    else {
        GameplayTooltipCostValues costs{};
        costs.values[0] = equipment_id;
        state.current_costs = costs;
    }
    state.cost_row_text.clear();
    DrawGameplayTooltipCostBox(state);
}

void BuildSimpleEquipmentTooltip(GameplayTooltipState& state) {
    state.current_text =
        platform_text_or_fallback(state, 75, state.current_text);
    DrawGameplayTooltipTextBox(state);
}

void BuildNumberedSimpleTooltip(GameplayTooltipState& state) {
    state.current_text =
        platform_text_or_fallback(state, 76, state.current_text);
    const u32 value = numbered_tooltip_tile_value(state);
    std::ostringstream text;
    text << state.current_text << value;
    state.current_text = text.str();
    DrawGameplayTooltipTextBox(state);
}

void BuildMultilineTooltip(GameplayTooltipState& state) {
    if (state.multiline_text.empty()) {
        for (std::size_t row = 89; row < 93; ++row) {
            if (row < state.platform_text_rows.size() &&
                !state.platform_text_rows[row].empty()) {
                state.multiline_text.push_back(state.platform_text_rows[row]);
            }
        }
    }
    DrawGameplayTooltipMultilineBox(state);
}

void BuildProductionOrderTooltip(GameplayTooltipState& state) {
    const u32 order_id = state.current_object_id >= 0xf4 ?
        state.current_object_id - 0xf4 : state.current_object_id;
    const GameplayTooltipProductionOrderDefinition* order =
        find_order_definition(state, order_id);
    state.current_text =
        order != nullptr && has_text(order->name) ?
        order->name : fallback_label("Order", order_id);
    state.secondary_text.clear();

    if ((state.hover_flags & kHoverFlagRequirements) != 0) {
        state.current_text += platform_text_or_fallback(state, 84, " Requirement:");
        state.requirement_text = order != nullptr ?
            requirement_label(state, order->prerequisites) : std::string{"Requirement"};
        DrawGameplayTooltipRequirementBox(state);
        return;
    }

    const u32 variant = local_production_order_variant(state, order_id);
    if (order != nullptr && order->variant_count > 1) {
        state.secondary_text =
            platform_text_or_fallback(state, 124, "Next level ") +
            std::to_string(variant + 1);
    }
    state.current_costs = production_order_costs(state, order_id, variant);
    state.cost_row_text.clear();
    draw_cost_or_simple(state);
}

}
