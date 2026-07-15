#include "ranker_gameplay_session_runtime.h"
#include "ranker_ui_overlay.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace ranker {

bool SurfacePixelMode555() {
    return false;
}

} // namespace ranker

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool same_rect(const UiOverlayRect& a, const UiOverlayRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width &&
        a.height == b.height;
}

struct FixedRecord {
    i32 x;
    i32 y;
    UiOverlayRect bounds;
};

constexpr FixedRecord fixed(i32 x, i32 y, i32 bx, i32 by, u32 w, u32 h) {
    return {x, y, {bx, by, w, h}};
}

constexpr std::array<std::array<FixedRecord, 4>, 3> kSmall1{{
    {{fixed(364,372,364,372,65,26), fixed(364,372,364,372,65,26),
      fixed(364,372,364,372,65,26), fixed(364,372,364,372,65,26)}},
    {{fixed(460,464,460,464,81,31), fixed(460,467,460,467,84,29),
      fixed(467,467,467,467,80,27), fixed(457,468,457,468,86,26)}},
    {{fixed(390,636,399,648,57,24), fixed(396,641,401,652,50,17),
      fixed(396,641,401,652,50,17), fixed(396,643,397,650,58,19)}},
}};

constexpr std::array<std::array<FixedRecord, 4>, 3> kSmall2{{
    {{fixed(213,375,213,375,29,19), fixed(213,375,213,375,29,19),
      fixed(213,375,213,375,29,19), fixed(213,375,213,375,29,19)}},
    {{fixed(273,464,273,464,40,31), fixed(273,466,273,466,41,30),
      fixed(276,467,276,467,29,27), fixed(269,468,269,468,39,26)}},
    {{fixed(542,641,555,649,50,22), fixed(544,644,556,653,42,19),
      fixed(544,644,556,653,42,19), fixed(542,646,554,651,44,18)}},
}};

constexpr std::array<std::array<FixedRecord, 4>, 3> kSmall3{{
    {{fixed(178,372,178,372,31,24), fixed(178,372,178,372,31,24),
      fixed(178,372,178,372,31,24), fixed(178,372,178,372,31,24)}},
    {{fixed(230,464,230,464,36,31), fixed(229,467,229,467,39,29),
      fixed(227,467,227,467,44,27), fixed(227,468,227,468,38,26)}},
    {{fixed(384,722,392,736,60,20), fixed(385,721,388,731,57,34),
      fixed(385,721,388,731,57,34), fixed(384,722,388,734,61,18)}},
}};

constexpr std::array<UiOverlayRect, 7> kManual{{
    {266,523,38,38}, {175,570,19,19}, {197,570,19,19},
    {223,570,19,19}, {245,570,19,19}, {267,570,19,19},
    {289,570,19,19},
}};

void check_fixed(const UiOverlayState& state, u32 bucket, u32 theme) {
    const FixedRecord& s1 = kSmall1[bucket][theme];
    const FixedRecord& s2 = kSmall2[bucket][theme];
    const FixedRecord& s3 = kSmall3[bucket][theme];
    require(state.small_slot1_x == s1.x && state.small_slot1_y == s1.y &&
            same_rect(state.small_slot1_bounds, s1.bounds),
        "small slot 1 outer/bounds mismatch");
    require(state.small_slot2_x == s2.x && state.small_slot2_y == s2.y &&
            same_rect(state.small_slot2_bounds, s2.bounds),
        "small slot 2 outer/bounds mismatch");
    require(state.small_slot3_x == s3.x && state.small_slot3_y == s3.y &&
            same_rect(state.small_slot3_bounds, s3.bounds),
        "small slot 3 outer/bounds mismatch");

    std::array<FixedRecord, 5> large{};
    if (bucket == 0) {
        large.fill(fixed(178,372,178,372,31,24));
    } else if (bucket == 1) {
        large = {{fixed(570,450,570,450,38,38),
                  fixed(616,450,616,450,38,38),
                  fixed(662,450,662,450,38,38),
                  fixed(708,450,708,450,38,38),
                  fixed(754,450,754,450,38,38)}};
    } else {
        large.fill(kSmall3[bucket][theme]);
    }
    require(state.large_slot_x == large[0].x && state.large_slot_y == large[0].y &&
            same_rect(state.large_slot0_bounds, large[0].bounds),
        "large slot 0 mismatch");
    require(state.large_slot3_x == large[1].x && state.large_slot3_y == large[1].y &&
            same_rect(state.large_slot3_bounds, large[1].bounds),
        "large slot 3 mismatch");
    require(state.large_slot4_x == large[2].x && state.large_slot4_y == large[2].y &&
            same_rect(state.large_slot6_bounds, large[2].bounds),
        "large slot 6 mismatch");
    require(state.large_slot5_x == large[3].x && state.large_slot5_y == large[3].y &&
            same_rect(state.large_slot9_bounds, large[3].bounds),
        "large slot 9 mismatch");
    require(state.large_slot6_x == large[4].x && state.large_slot6_y == large[4].y &&
            same_rect(state.large_slot12_bounds, large[4].bounds),
        "large slot 12 mismatch");
}

void check_panel_tables(const UiOverlayState& state, u32 bucket) {
    const std::array<i32, 8> dynamic_x_800{{464,505,546,587,628,669,710,751}};
    const std::array<i32, 8> dynamic_x_other{{370,403,436,469,502,535,568,601}};
    const auto& dynamic_x = bucket == 1 ? dynamic_x_800 : dynamic_x_other;
    const i32 first_y = bucket == 1 ? 513 : 407;
    const i32 second_y = bucket == 1 ? 554 : 440;
    for (u32 i = 0; i < 16; ++i) {
        require(same_rect(state.dynamic_icon_bounds[i],
                {dynamic_x[i % 8], i < 8 ? first_y : second_y, 38, 38}),
            "dynamic 16-slot table mismatch");
    }

    const std::array<i32, 6> command_x_800{{468,521,574,627,680,733}};
    const std::array<i32, 6> command_x_other{{373,416,459,502,545,588}};
    const auto& command_x = bucket == 1 ? command_x_800 : command_x_other;
    for (u32 i = 0; i < 6; ++i) {
        require(same_rect(state.command_slot_bounds[i],
                {command_x[i], bucket == 1 ? 530 : 421, 50, 50}),
            "command 6-slot table mismatch");
    }

    const std::array<i32, 7> side_x_800{{15,56,97,138,179,220,261}};
    const std::array<i32, 7> side_x_other{{12,45,78,111,144,177,210}};
    const auto& side_x = bucket == 1 ? side_x_800 : side_x_other;
    for (u32 i = 0; i < 14; ++i) {
        i32 y = 0;
        if (bucket == 0) y = (i % 2 == 0) ? 407 : 440;
        if (bucket == 1) y = (i % 2 == 0) ? 513 : 554;
        if (bucket == 2) y = (i % 2 == 0) ? 73 : 40;
        require(same_rect(state.side_slot_bounds[i],
                {side_x[i / 2], y, 38, 38}),
            "side 14-slot table mismatch");
    }

    const std::array<UiOverlayRect, 5> indexed_800{{
        {84,537,38,38}, {132,549,38,38}, {175,549,38,38},
        {218,549,38,38}, {261,549,38,38}}};
    const std::array<UiOverlayRect, 5> indexed_other{{
        {74,440,38,38}, {113,440,38,38}, {146,440,38,38},
        {179,440,38,38}, {212,440,38,38}}};
    const auto& indexed = bucket == 1 ? indexed_800 : indexed_other;
    require(state.indexed_slot_bounds.size() == indexed.size(),
        "indexed 5-slot table size mismatch");
    for (u32 i = 0; i < indexed.size(); ++i) {
        require(same_rect(state.indexed_slot_bounds[i], indexed[i]),
            "indexed 5-slot table mismatch");
    }

    for (u32 i = 0; i < kManual.size(); ++i) {
        require(same_rect(state.manual_equipment_slot_bounds[i], kManual[i]),
            "manual equipment 7-slot table mismatch");
    }
    require(same_rect(state.wide_slot_bounds, {19,518,50,50}),
        "fixed wide-slot anchor mismatch");
}

void test_all_layouts_and_themes() {
    const std::array<u32, 3> widths{{640,800,1024}};
    constexpr std::array<std::array<i32, 4>, 3> kCameraAnchorY{{
        {{178,181,178,182}},
        {{219,218,210,226}},
        {{223,229,223,230}},
    }};
    for (u32 bucket = 0; bucket < widths.size(); ++bucket) {
        for (u32 theme = 0; theme < 4; ++theme) {
            UiOverlayState state{};
            state.screen_width = widths[bucket];
            state.screen_height = bucket == 0 ? 480 : (bucket == 1 ? 600 : 768);
            state.interface_theme_index = theme;
            ConfigureGameplayUiOverlayLayout(state);
            require(state.screen_layout_bucket == bucket,
                "screen width must select original bucket");
            require(state.minimap_camera_anchor_x ==
                    static_cast<i32>(widths[bucket] / 2) &&
                    state.minimap_camera_anchor_y ==
                        kCameraAnchorY[bucket][theme],
                "camera viewport anchor table mismatch");
            check_fixed(state, bucket, theme);
            check_panel_tables(state, bucket);
        }
    }
}

void test_logical_surface_resolution_priority() {
    GameplayLogicalSurfaceSize surface = ResolveGameplayLogicalSurfaceSize(
        true, 640, 480, 800, 600);
    require(surface.width == 640 && surface.height == 480,
        "active DirectDraw surface must override the requested display state");

    surface = ResolveGameplayLogicalSurfaceSize(false, 640, 480, 1024, 768);
    require(surface.width == 1024 && surface.height == 768,
        "inactive DirectDraw dimensions must not leak after surface teardown");

    surface = ResolveGameplayLogicalSurfaceSize(false, 640, 480, 0, 768);
    require(surface.width == kGameplayDefaultScreenWidth &&
            surface.height == kGameplayDefaultScreenHeight,
        "incomplete display state must fall back to the original dimensions");
}

}  // namespace

int main() {
    test_all_layouts_and_themes();
    test_logical_surface_resolution_priority();
    std::cout << "UI_LAYOUT_BUCKET_COMPLETION_PASS buckets=3 themes=4\n";
    return EXIT_SUCCESS;
}
