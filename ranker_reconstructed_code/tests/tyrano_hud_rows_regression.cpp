#include "ranker_text_renderer.h"
#include "ranker_ui_overlay.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace ranker {

bool SurfacePixelMode555() {
    return false;
}

bool DrawResourceSpriteNormal(u32, i32, i32) {
    return false;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

const UiOverlayDrawRecord& record_for(
    const UiOverlayState& state, u32 item_id) {
    const auto found = std::find_if(state.queued_records.begin(),
        state.queued_records.end(), [item_id](const UiOverlayDrawRecord& record) {
            return record.item_id == item_id;
        });
    require(found != state.queued_records.end(),
        "expected Tyrano command record is missing");
    return *found;
}

const UiOverlayTextCommand& text_for(
    const UiOverlayState& state, const std::string& text) {
    const auto found = std::find_if(state.text_commands.begin(),
        state.text_commands.end(), [&text](const UiOverlayTextCommand& command) {
            return command.text == text;
        });
    require(found != state.text_commands.end(),
        "expected top-right HUD text command is missing");
    return *found;
}

UiOverlayState make_tyrano_state(u32 width, u32 height, u32 theme) {
    UiOverlayState state{};
    state.screen_width = width;
    state.screen_height = height;
    state.interface_theme_index = theme;
    state.selected_unit_count = 1;
    state.selected_unit_type = 0x80;
    state.selected_unit_owner = 2;
    state.local_player_slot = 2;
    state.selected_unit_raw_production_reference_count = 2;
    state.primary_production_options.push_back({0x20, 0, 0, 0, 0, true});
    state.primary_production_options.push_back({0x2c, 0, 0, 0, 0, true});
    state.command_options.push_back({0x108, 0, 0, 0, 0, true});
    state.command_options.push_back({0x11e, 0, 0, 0, 0, true});
    state.command_options.push_back({0x12c, 0, 0, 0, 0, true});
    state.command_options.push_back({0x11f, 0, 0, 0, 0, true});
    ConfigureGameplayUiOverlayLayout(state);
    ResetUiOverlayCommandPanelState(state);
    BuildSingleSelectedUnitCommandPanel(state);
    return state;
}

void verify_tyrano_rows_and_hit_bounds(
    u32 width, u32 height, u32 theme, u32 bucket) {
    UiOverlayState state = make_tyrano_state(width, height, theme);
    const std::array<i32, 8> x_640_or_other{
        370, 403, 436, 469, 502, 535, 568, 601};
    const std::array<i32, 8> x_800{
        464, 505, 546, 587, 628, 669, 710, 751};
    const auto& x = bucket == 1 ? x_800 : x_640_or_other;
    const i32 first_y = bucket == 1 ? 513 : 407;
    const i32 second_y = bucket == 1 ? 554 : 440;

    require(state.screen_layout_bucket == bucket,
        "screen width selected the wrong original layout bucket");
    require(state.queued_records.size() == 12,
        "two production + c9 + five pads + four upgrades must emit 12 records");
    require(state.hot_regions.size() == state.queued_records.size(),
        "every queued Tyrano record must retain the same hit rectangle");

    const std::array<u32, 3> first_row_items{0x20, 0x2c, 0xc9};
    for (std::size_t index = 0; index < first_row_items.size(); ++index) {
        const UiOverlayDrawRecord& record = record_for(state, first_row_items[index]);
        require(record.x == x[index] && record.y == first_y &&
                record.width == 0x26 && record.height == 0x26,
            "Tyrano first-row draw bounds differ from DAT_0086444c");
    }

    for (std::size_t index = 3; index < 8; ++index) {
        const UiOverlayDrawRecord& record = state.queued_records[index];
        require(record.item_id == 0xc8 && record.flags == 2u &&
                record.x == x[index] && record.y == first_y,
            "FUN_004e5269 padding must consume the remaining first-row slots");
    }

    const std::array<u32, 4> upgrade_items{0x108, 0x11e, 0x12c, 0x11f};
    for (std::size_t index = 0; index < upgrade_items.size(); ++index) {
        const UiOverlayDrawRecord& record = record_for(state, upgrade_items[index]);
        require(record.x == x[index] && record.y == second_y &&
                record.width == 0x26 && record.height == 0x26,
            "Tyrano upgrade draw bounds must begin at dynamic slot eight");
        const UiOverlayHotRegion& hit = state.hot_regions[8 + index];
        require(hit.record.item_id == record.item_id &&
                hit.record.x == record.x && hit.record.y == record.y &&
                hit.record.width == record.width &&
                hit.record.height == record.height,
            "Tyrano upgrade draw and hit bounds diverged");
        require(HitTestUiOverlayHotRegion(
                    state, record.x + 0x12, record.y + 0x12) &&
                state.last_hotkey_command == record.item_id,
            "Tyrano second-row upgrade is not clickable inside its draw bounds");
    }
}

void verify_top_right_hud(u32 width, u32 height, u32 theme, u32 bucket) {
    UiOverlayState state{};
    state.screen_width = width;
    state.screen_height = height;
    state.interface_theme_index = theme;
    state.local_player_type = 4;
    state.resource_amount = 321;
    state.population_used = 12;
    state.population_available = 7;
    state.population_limit = 9;
    state.emit_sprite_draws = false;
    ConfigureGameplayUiOverlayLayout(state);
    RenderGameplayResourceCounters(state);

    require(state.screen_layout_bucket == bucket,
        "HUD fixture selected the wrong original layout bucket");
    require(state.resource_counter_x == static_cast<i32>(width) - 0x8c &&
            state.resource_counter_y == 5,
        "resource icon anchor differs from DAT_008634ec/f0");
    require(state.population_counter_x == static_cast<i32>(width) - 0x46 &&
            state.population_counter_y == 5,
        "population icon anchor differs from DAT_00863504/08");

    const UiOverlayTextCommand& resource = text_for(state, "321");
    const UiOverlayTextCommand& used = text_for(state, "12");
    const UiOverlayTextCommand& slash = text_for(state, "/");
    const UiOverlayTextCommand& available = text_for(state, "7");
    require(resource.x == static_cast<i32>(width) - 0x8c + 0x12 &&
            resource.y == 7 && resource.color == 1,
        "resource counter text anchor/color differs from FUN_004e2a12");
    require(used.x == static_cast<i32>(width) - 0x46 + 0x12 &&
            used.y == 7 && used.color == 9,
        "population-used text anchor/warning color differs from FUN_004e2a12");
    require(slash.x == used.x + 12 && slash.y == 7 && slash.color == 1,
        "population slash must follow the font-1 width of two digits");
    require(available.x == slash.x + 6 && available.y == 7 &&
            available.color == 1,
        "population capacity must follow the font-1 slash glyph");
    for (const UiOverlayTextCommand* command :
            {&resource, &used, &slash, &available}) {
        require(command->draw_font == 1 &&
                command->metric_font == kUiOverlayPreserveMetricFont,
            "top-right HUD must select original draw font 1 only");
    }
}

}  // namespace

int main() {
    ResetTextRendererState();
    RegisterTextFontDefinition(1, 2, 6, 11, nullptr);
    const std::array<u32, 3> widths{640, 800, 1024};
    const std::array<u32, 3> heights{480, 600, 768};
    for (u32 bucket = 0; bucket < 3; ++bucket) {
        for (u32 theme = 0; theme < 4; ++theme) {
            verify_tyrano_rows_and_hit_bounds(
                widths[bucket], heights[bucket], theme, bucket);
            verify_top_right_hud(
                widths[bucket], heights[bucket], theme, bucket);
        }
    }
    std::cout << "TYRANO_HUD_ROWS_PASS buckets=3 themes=4 draw_hit=exact font=1\n";
    return EXIT_SUCCESS;
}
