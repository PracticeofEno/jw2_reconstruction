#include "ranker_ui_overlay.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> g_draw_events;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "selected info draw-order regression failed at line " << line
              << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail(#expression, __LINE__); \
        } \
    } while (false)

void draw_selected_record(ranker::UiOverlayState& state) {
    g_draw_events.emplace_back("selected-record");

    ranker::UiOverlayProgressCommand progress{};
    progress.left = 10;
    progress.top = 20;
    progress.right = 110;
    progress.bottom = 22;
    progress.numerator = 1;
    progress.denominator = 2;
    state.progress_commands.push_back(progress);

    ranker::UiOverlayTextCommand name{};
    name.text = "A VERY LONG SELECTED UNIT NAME";
    name.x = 30;
    name.y = 40;
    name.color = 1;
    name.draw_font = 4;
    name.metric_font = 4;
    state.text_commands.push_back(name);
}

void flush_progress_suffix(ranker::UiOverlayState& state,
    std::size_t first_command) {
    for (std::size_t index = first_command;
         index < state.progress_commands.size(); ++index) {
        g_draw_events.emplace_back("selected-progress");
    }
}

void flush_text_suffix(ranker::UiOverlayState& state,
    std::size_t first_command) {
    for (std::size_t index = first_command;
         index < state.text_commands.size(); ++index) {
        g_draw_events.emplace_back(
            std::string("selected-name:") + state.text_commands[index].text);
    }
}

void draw_progress_frame(ranker::UiOverlayState&) {
    g_draw_events.emplace_back("selected-progress-frame");
}

} // namespace

int main() {
    ranker::UiOverlayState state{};
    state.emit_sprite_draws = true;

    // Commands remain available to read-only diagnostics after drawing.  The
    // product callbacks maintain parallel flushed markers so the frame-tail
    // pass can skip commands already emitted at their record boundary.
    ranker::UiOverlayProgressCommand preexisting_progress{};
    preexisting_progress.denominator = 1;
    state.progress_commands.push_back(preexisting_progress);
    ranker::UiOverlayTextCommand preexisting_text{};
    preexisting_text.text = "preexisting";
    state.text_commands.push_back(preexisting_text);

    ranker::DrawUiOverlayRecordAndFlushSuffixCore(state, draw_selected_record,
        flush_text_suffix, draw_progress_frame, flush_progress_suffix);
    g_draw_events.emplace_back("following-command-icon");

    REQUIRE(g_draw_events.size() == 5u);
    REQUIRE(g_draw_events[0] == "selected-record");
    REQUIRE(g_draw_events[1] ==
        "selected-name:A VERY LONG SELECTED UNIT NAME");
    REQUIRE(g_draw_events[2] == "selected-progress-frame");
    REQUIRE(g_draw_events[3] == "selected-progress");
    REQUIRE(g_draw_events[4] == "following-command-icon");

    REQUIRE(state.progress_commands.size() == 2u);
    REQUIRE(state.text_commands.size() == 2u);
    REQUIRE(state.text_commands.front().text == "preexisting");
    REQUIRE(state.progress_command_flushed.size() == 2u);
    REQUIRE(state.progress_command_flushed[0] == 0u);
    REQUIRE(state.progress_command_flushed[1] == 1u);
    REQUIRE(state.text_command_flushed.size() == 2u);
    REQUIRE(state.text_command_flushed[0] == 0u);
    REQUIRE(state.text_command_flushed[1] == 1u);

    std::cout << "SELECTED_INFO_DRAW_ORDER_PASS "
                 "record>name>progress-frame>progress>following-icon "
                 "streams-preserved\n";
    return EXIT_SUCCESS;
}
