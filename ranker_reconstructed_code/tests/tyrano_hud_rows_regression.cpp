#include "ranker_directx.h"
#include "ranker_runtime_resources.h"
#include "ranker_sprite_renderer.h"
#include "ranker_system_ui.h"
#include "ranker_text_renderer.h"
#include "ranker_ui_overlay.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace ranker {

i32 g_progress_fill_left = 0;
i32 g_progress_fill_top = 0;
i32 g_progress_fill_right = 0;
i32 g_progress_fill_bottom = 0;
u16 g_progress_fill_color = 0;
u32 g_progress_fill_count = 0;

bool SurfacePixelMode555() {
    return false;
}

bool DrawResourceSpriteNormal(u32, i32, i32) {
    return false;
}

bool DrawResourceSpriteDirectToken1Shadow(u32, i32, i32) {
    return false;
}

u32 GetUnitDefinitionImageResourceEntry(u32, u32) {
    return kInvalidResourceEntry;
}

const SpriteRenderState& sprite_render_state() {
    static const SpriteRenderState state{};
    return state;
}

RankerSystemUiState& system_ui_state() {
    static RankerSystemUiState state{};
    return state;
}

bool InitializeWin32UiFontMetrics(HDC) {
    return false;
}

void InitializeUiFontHandles() {
}

const DirectDrawRuntimeState& direct_draw_state() {
    static const DirectDrawRuntimeState state{};
    return state;
}

const CommandThemeResourceState& command_theme_resource_state() {
    static const CommandThemeResourceState state{};
    return state;
}

bool DrawBackBufferStippledRectangle16(
    i32 left, i32 top, i32 right, i32 bottom, u16 color) {
    g_progress_fill_left = left;
    g_progress_fill_top = top;
    g_progress_fill_right = right;
    g_progress_fill_bottom = bottom;
    g_progress_fill_color = color;
    ++g_progress_fill_count;
    return true;
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

const UiOverlayProgressCommand& only_progress_command(
    const UiOverlayState& state) {
    require(state.progress_commands.size() == 1,
        "selected construction must retain one diagnostic progress command");
    return state.progress_commands.front();
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

void verify_structure_padding_exceptions(
    u32 width, u32 height, u32 theme, u32 bucket) {
    const std::array<i32, 8> x_640_or_other{
        370, 403, 436, 469, 502, 535, 568, 601};
    const std::array<i32, 8> x_800{
        464, 505, 546, 587, 628, 669, 710, 751};
    const auto& x = bucket == 1 ? x_800 : x_640_or_other;
    const i32 first_y = bucket == 1 ? 513 : 407;
    const i32 second_y = bucket == 1 ? 554 : 440;

    UiOverlayState ordinary{};
    ordinary.screen_width = width;
    ordinary.screen_height = height;
    ordinary.interface_theme_index = theme;
    ordinary.selected_unit_count = 1;
    ordinary.selected_unit_type = 0x68;
    ordinary.selected_unit_owner = 0;
    ordinary.local_player_slot = 0;
    ordinary.command_options.push_back({0xf7, 0, 0, 0, 0, true});
    ConfigureGameplayUiOverlayLayout(ordinary);
    ResetUiOverlayCommandPanelState(ordinary);
    BuildSingleSelectedUnitCommandPanel(ordinary);
    require(ordinary.queued_records.size() == 9,
        "ordinary structure must pad eight slots before its first order");
    for (std::size_t index = 0; index < 8; ++index) {
        const UiOverlayDrawRecord& record = ordinary.queued_records[index];
        require(record.item_id == 0xc8 && record.flags == 2u &&
                record.x == x[index] && record.y == first_y,
            "ordinary structure padding differs from FUN_004e5269");
    }
    const UiOverlayDrawRecord& ordinary_order = record_for(ordinary, 0xf7);
    require(ordinary_order.x == x[0] && ordinary_order.y == second_y,
        "ordinary empty-production structure order did not begin on row two");

    UiOverlayState blacksmith{};
    blacksmith.screen_width = width;
    blacksmith.screen_height = height;
    blacksmith.interface_theme_index = theme;
    blacksmith.selected_unit_count = 1;
    blacksmith.selected_unit_type = 0x67;
    blacksmith.selected_unit_owner = 0;
    blacksmith.local_player_slot = 0;
    blacksmith.command_options.push_back({0x252, 0, 0, 0, 0, true});
    ConfigureGameplayUiOverlayLayout(blacksmith);
    ResetUiOverlayCommandPanelState(blacksmith);
    BuildSingleSelectedUnitCommandPanel(blacksmith);
    require(blacksmith.queued_records.size() == 1,
        "type-0x67 special case must not synthesize first-row padding");
    const UiOverlayDrawRecord& equipment = record_for(blacksmith, 0x252);
    require(equipment.x == x[0] && equipment.y == first_y,
        "Blacksmith equipment did not retain the original first-row slot");

    UiOverlayState avatar{};
    avatar.screen_width = width;
    avatar.screen_height = height;
    avatar.interface_theme_index = theme;
    avatar.selected_unit_count = 1;
    avatar.selected_unit_type = 0x6f;
    avatar.selected_unit_owner = 0;
    avatar.local_player_slot = 0;
    avatar.selected_unit_uses_avatar_production_slots = true;
    avatar.primary_production_options.push_back({5, 1, 0, 0, 0, true});
    avatar.command_options.push_back({0x24a, 0, 0, 0, 0, true});
    ConfigureGameplayUiOverlayLayout(avatar);
    ResetUiOverlayCommandPanelState(avatar);
    BuildSingleSelectedUnitCommandPanel(avatar);
    require(avatar.queued_records.size() == 9,
        "avatar producer must publish its slot, c9, padding, and equipment");
    const UiOverlayDrawRecord& avatar_slot = record_for(avatar, 5);
    const UiOverlayDrawRecord& avatar_queue = record_for(avatar, 0xc9);
    const UiOverlayDrawRecord& avatar_equipment = record_for(avatar, 0x24a);
    require(avatar_slot.x == x[0] && avatar_slot.y == first_y &&
            avatar_queue.x == x[1] && avatar_queue.y == first_y &&
            avatar_equipment.x == x[0] && avatar_equipment.y == second_y,
        "avatar producer row transition differs from FUN_004e3f6e");
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

void verify_selected_construction_info(
    u32 width, u32 height, u32 theme, u32 bucket) {
    UiOverlayState state{};
    state.screen_width = width;
    state.screen_height = height;
    state.interface_theme_index = theme;
    state.current_detail_item_id = 0x82;
    state.selected_unit_count = 1;
    state.selected_unit_id = 0x1d0;
    state.selected_unit_type = 0x82;
    state.selected_unit_owner = 2;
    state.local_player_slot = 2;
    state.selected_unit_health = 37;
    state.selected_unit_health_ratio_max = 100;
    state.selected_unit_health_text_color = 0x11;
    state.selected_unit_name_text = "CONSTRUCTION";
    state.selected_unit_details_visible = true;
    state.selected_unit_action_mode_gate = 1;
    state.detail_progress = 73;
    state.detail_progress_total = 200;
    state.emit_sprite_draws = false;
    ConfigureGameplayUiOverlayLayout(state);

    // Dispatch record 0x1a6 temporarily uses this fixed wide-slot anchor.
    state.large_slot_x = state.wide_slot_bounds.x;
    state.large_slot_y = state.wide_slot_bounds.y;
    RenderSelectedUnitInfoPanel(state);

    const UiOverlayTextCommand& health = text_for(state, "37/100");
    require(health.x == 44 && health.y == 576 && health.color == 0x11 &&
            health.draw_font == 1 && health.metric_font == 3 &&
            health.centered,
        "construction HP position/color/font differs from FUN_004e1544");
    const UiOverlayTextCommand& name = text_for(state, "CONSTRUCTION");
    require(name.x == 75 && name.y == 519 && name.color == 1 &&
            name.draw_font == 4 && name.metric_font == 4 &&
            !name.centered,
        "construction name position/color/font differs from FUN_004e1544");

    constexpr std::array<UiOverlayRect, 3> kOriginalProgressBounds{{
        {77, 433, 98, 2}, {129, 537, 126, 2}, {77, 433, 98, 2},
    }};
    const UiOverlayRect expected = kOriginalProgressBounds[bucket];
    const UiOverlayProgressCommand& progress = only_progress_command(state);
    require(progress.left == expected.x && progress.top == expected.y &&
            progress.right == expected.x + static_cast<i32>(expected.width) &&
            progress.bottom == expected.y + static_cast<i32>(expected.height),
        "construction progress endpoints differ from DAT_008642dc");
    require(progress.numerator == 73 && progress.denominator == 200,
        "construction progress numerator/denominator were not preserved");

    // A completed record leaves its diagnostic command in the vector even
    // after immediate drawing.  A live empty snapshot can therefore only be
    // a sample taken in reset_frame_output_commands()'s short clear window.
    require(state.progress_commands.size() == 1,
        "selected-info diagnostic progress stream was cleared after rendering");

    // Exercise the real 0x1a6 immediate-flush path as well.  The 565 fixture
    // must derive the original three-quarter green and use inclusive fill
    // endpoints after floor(width * numerator / denominator).
    state.text_commands.clear();
    state.progress_commands.clear();
    state.text_command_flushed.clear();
    state.progress_command_flushed.clear();
    state.emit_sprite_draws = true;
    InstallDefaultUiOverlayDispatchHandlers(state);
    UiOverlayDrawRecord selected_record{};
    selected_record.item_id = 0x1a6;
    g_progress_fill_count = 0;
    require(DispatchUiOverlayDrawRecord(state, selected_record),
        "selected-info dispatch record was rejected");
    const i32 fill_width = static_cast<i32>(
        (static_cast<u64>(expected.width) * 73u) / 200u);
    require(g_progress_fill_count == 1 &&
            g_progress_fill_left == expected.x &&
            g_progress_fill_top == expected.y &&
            g_progress_fill_right == expected.x + fill_width &&
            g_progress_fill_bottom ==
                expected.y + static_cast<i32>(expected.height) &&
            g_progress_fill_color == 0x05e0,
        "construction progress fill endpoint/color differs from FUN_004e1544");
    require(state.progress_commands.size() == 1 &&
            state.progress_command_flushed.size() == 1 &&
            state.progress_command_flushed.front() == 1,
        "immediate progress draw must preserve and mark its diagnostic command");
}

void verify_construction_hides_production_buttons(
    u32 width, u32 height, u32 theme, u32 bucket) {
    UiOverlayState state{};
    state.screen_width = width;
    state.screen_height = height;
    state.interface_theme_index = theme;
    state.selected_unit_count = 1;
    state.selected_unit_id = 0x1d0;
    state.selected_unit_type = 0x82;
    state.selected_unit_owner = 2;
    state.local_player_slot = 2;
    state.selected_unit_action_mode_gate = 1;
    state.selected_unit_command_bit_mask = 0xffffffffu;
    state.selected_unit_raw_production_reference_count = 2;
    state.primary_production_options.push_back({0x20, 0, 0, 0, 0, true});
    state.primary_production_options.push_back({0x2c, 0, 0, 0, 0, true});
    state.command_options.push_back({0x108, 0, 0, 0, 0, true});
    ConfigureGameplayUiOverlayLayout(state);
    BuildSelectedUnitCommandPanel(state);

    const auto count_item = [&state](u32 item) {
        return static_cast<std::size_t>(std::count_if(
            state.queued_records.begin(), state.queued_records.end(),
            [item](const UiOverlayDrawRecord& record) {
                return record.item_id == item;
            }));
    };
    require(count_item(0x1a6) == 1,
        "construction panel must retain the selected-info record");
    require(count_item(0xc6) == 1,
        "construction panel must expose exactly one large cancel button");
    require(count_item(0x20) == 0 && count_item(0x2c) == 0 &&
            count_item(0x108) == 0,
        "ordinary production/upgrade buttons leaked into construction mode");

    const UiOverlayDrawRecord& cancel = record_for(state, 0xc6);
    const std::array<UiOverlayRect, 3> expected_cancel{{
        {373, 421, 50, 50}, {468, 530, 50, 50}, {373, 421, 50, 50},
    }};
    const UiOverlayRect expected = expected_cancel[bucket];
    require(cancel.aux == 3 && cancel.flags == 0 &&
            cancel.x == expected.x && cancel.y == expected.y &&
            cancel.width == expected.width && cancel.height == expected.height,
        "construction cancel button differs from FUN_004e4e93/FUN_004e5762");
    require(state.queued_records.size() == 5,
        "construction panel must contain defaults, selected info, and cancel only");
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
            verify_structure_padding_exceptions(
                widths[bucket], heights[bucket], theme, bucket);
            verify_top_right_hud(
                widths[bucket], heights[bucket], theme, bucket);
            verify_selected_construction_info(
                widths[bucket], heights[bucket], theme, bucket);
            verify_construction_hides_production_buttons(
                widths[bucket], heights[bucket], theme, bucket);
        }
    }
    std::cout << "TYRANO_HUD_ROWS_PASS buckets=3 themes=4 draw_hit=exact "
                 "padding_exceptions=exact font=1 construction_info=exact "
                 "production_hidden\n";
    return EXIT_SUCCESS;
}
