#include "ranker_gameplay_input_actions.h"
#include "ranker_gameplay_packets.h"
#include "ranker_reliable_packets.h"
#include "ranker_unit_animation.h"

#ifdef _WIN32
#include "ranker_cursor.h"
#include "ranker_screenshot.h"
#endif

#include <cstdlib>
#include <iostream>

namespace ranker {

Mode1ReliableRuntimeState& mode1_reliable_state() {
    static Mode1ReliableRuntimeState state{};
    return state;
}

void ResetMode1GameplayVoteCompletionGate() {}

#ifdef _WIN32
SoftwareCursorState& software_cursor_state() {
    static SoftwareCursorState state{};
    return state;
}

void SetGameCursorPointerPosition(i32, i32) {}
void RequestScreenshotCapture() {}
void SetContinuousScreenshotCapture(bool) {}
#endif

} // namespace ranker

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "attack target red flash regression failed at line " << line
              << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) \
    do { if (!(expression)) { fail(#expression, __LINE__); } } while (false)

u32 g_feedback_unit = 0;
u32 g_feedback_flags = 0;
UnitAnimationDrawKind g_draw_kind = UnitAnimationDrawKind::normal;
u32 g_draw_count = 0;

void capture_feedback(
    GameplayInputActionState&, u32 unit_offset, u32 draw_flags) {
    g_feedback_unit = unit_offset;
    g_feedback_flags = draw_flags;
}

void capture_draw(
    UnitAnimationDrawContext&, const UnitAnimationDrawCommand& command) {
    ++g_draw_count;
    g_draw_kind = command.kind;
}

void test_physical_a_selector_target_path_sets_original_timer() {
    GameplayInputActionState state{};
    InitializeOriginalGameplayInputActionTables(state);
    state.callbacks.apply_unit_draw_flags = capture_feedback;
    state.local_player_index = 1;
    state.map_width_tiles = 128;
    state.map_height_tiles = 128;
    state.map_origin_x = 320;
    state.map_origin_y = 160;

    GameplayActionUnitState attacker{};
    attacker.offset = 0x1234u;
    attacker.type = 0x20u;
    attacker.owner = state.local_player_index;
    attacker.selected = true;
    attacker.flags = 0x80u;
    attacker.x = 512;
    attacker.y = 512;
    state.units.push_back(attacker);

    GameplayActionUnitState target{};
    target.offset = 0x5678u;
    target.type = 0x4bu;
    target.owner = 9;
    target.x = 960;
    target.y = 704;
    target.bounds_left = -16;
    target.bounds_top = -16;
    target.bounds_width = 32;
    target.bounds_height = 32;
    state.units.push_back(target);

    state.selected_unit_offset = attacker.offset;
    state.current_unit_offset = attacker.offset;
    state.multi_select_count = 1;
    state.current_snapshot.field2 = 0;

    constexpr u32 kAttackHudItem = 0xafu;
    constexpr u32 kObjectHudBase = 0xaau;
    constexpr u32 kAttackSelector = kAttackHudItem - kObjectHudBase;
    const i32 screen_x = target.x - static_cast<i32>(state.map_origin_x);
    const i32 screen_y = target.y - static_cast<i32>(state.map_origin_y);

    g_feedback_unit = 0;
    g_feedback_flags = 0;
    REQUIRE(DispatchSelectedUnitActionCommand(
        state, kAttackSelector, screen_x, screen_y, attacker.offset) ==
        kAttackSelector);
    REQUIRE(!state.last_dispatch_failed);
    REQUIRE(state.last_validation_unit_offset == target.offset);
    REQUIRE(state.units[0].draw_flags == 0u);
    REQUIRE(state.units[1].draw_flags == 0x88u);
    REQUIRE(g_feedback_unit == target.offset);
    REQUIRE(g_feedback_flags == 0x88u);
}

void test_timer_selects_four_original_red_render_phases() {
    UnitAnimationDefinition definition{};
    UnitAnimationDrawContext context{};
    context.definition = &definition;
    context.callbacks.draw_sprite = capture_draw;

    UnitAnimationUnit target{};
    target.type_id = 0x4bu;
    target.draw_flags = 0x88u;

    u32 red_phase_count = 0;
    for (;;) {
        g_draw_count = 0;
        DrawUnitDefaultAnimationFrame(context, target);
        REQUIRE(g_draw_count == 1u);
        const bool red_phase = (target.draw_flags & 0x82u) == 0x82u;
        REQUIRE((g_draw_kind == UnitAnimationDrawKind::mode_80) == red_phase);
        red_phase_count += red_phase ? 1u : 0u;

        if ((target.draw_flags & 0x7fu) == 0) {
            break;
        }
        --target.draw_flags;
    }

    REQUIRE(target.draw_flags == 0x80u);
    REQUIRE(red_phase_count == 4u);
}

} // namespace

int main() {
    test_physical_a_selector_target_path_sets_original_timer();
    test_timer_selects_four_original_red_render_phases();
    std::cout << "ATTACK_TARGET_RED_FLASH_PASS input=A/0xaf selector=5 "
                 "target=0x88 render=redx4 timer=0x80\n";
    return EXIT_SUCCESS;
}
