#include "ranker_gameplay_result_screen.h"

#include "ranker_cursor.h"
#include "ranker_directx.h"
#include "ranker_miles.h"
#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"
#include "ranker_runtime_resources.h"
#include "ranker_sprite_renderer.h"
#include "ranker_text_renderer.h"
#include "ranker_ui_screen.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace ranker {
namespace {

GameplayResultScreenState g_gameplay_result_screen_state;

void log_result_action(GameplayResultScreenState& state, GameplayResultActionKind kind,
    u32 value0 = 0, u32 value1 = 0, const std::string& text = {}) {
    state.action_log.push_back(GameplayResultAction{ kind, value0, value1, text });
}

u32 safe_metric_max(u32 value) {
    return value == 0 ? 1u : value;
}

std::array<u32, kGameplayResultMetricCount> metric_maxima_from_rows(
    const std::vector<GameplayResultRow>& rows) {
    std::array<u32, kGameplayResultMetricCount> maxima{};
    for (const GameplayResultRow& row : rows) {
        for (std::size_t metric = 0; metric < maxima.size(); ++metric) {
            maxima[metric] = std::max(maxima[metric], row.metrics[metric]);
        }
    }
    for (u32& value : maxima) {
        value = safe_metric_max(value);
    }
    return maxima;
}

void draw_metric_text(GameplayResultScreenState& state, i32 x, i32 y, u32 value,
    bool total = false) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%u", value);
    if (total) {
        DrawGameplayResultTotalTextLine(state, x, y, 1, text);
    }
    else {
        DrawGameplayResultTextLine(state, x, y, 1, text);
    }
}

void draw_result_elapsed_time(GameplayResultScreenState& state) {
    if (!state.generic_ai_profile_mode) {
        return;
    }

    char text[32]{};
    std::snprintf(text, sizeof(text), "%d : %d : %d",
        static_cast<int>(state.elapsed_hours),
        static_cast<int>(state.elapsed_minutes),
        static_cast<int>(state.elapsed_seconds));
    DrawGameplayResultTextLine(state, 0x1cc, 10, 0, text);
}

void draw_result_text_fallback(i32 x, i32 y, u8 color, const char* text) {
    SelectTextDrawFont(0);
    SetTextCursor(x, y, color);
    RenderAsciiOnlyTextLine(text != nullptr ? text : "");
}

void present_result_cursor_default() {
#ifdef _WIN32
    HandleGameCursorPresentation();
#endif
}

void set_result_music_policy_default(u32 result_mode) {
    SetPrimaryMilesMusicPolicyMode(result_mode == 0 ? 5u : 6u);
}

u32 result_outcome_sprite_token(u32 result_mode) {
    switch (result_mode) {
    case 0:
        return 0x03;
    case 1:
        return 0x04;
    case 2:
        return 0x05;
    case 3:
        return 0x06;
    default:
        return 0x05;
    }
}

void open_replay_result_dialog_default() {
    OpenReplayControlModalDialog();
}

void open_post_result_dialog_default() {
    OpenGameplayPostResultDialog();
}

u32 result_layout_bucket(u32 result_mode) {
    switch (result_mode) {
    case 1:
    case 3:
        return 1;
    default:
        return 0;
    }
}

void release_default_result_allocations(GameplayResultScreenState& state) {
    if (state.resource_mark != 0xffffffffu) {
        ReleaseResourceEntriesFrom(state.resource_mark);
    }
    else if (state.resource_base != 0xffffffffu) {
        ReleaseResourceEntriesFrom(state.resource_base);
    }
    if (state.palette_mark != 0xffffffffu) {
        ReleasePaletteCacheSlotsFrom(state.palette_mark);
    }
    state.resource_base = 0xffffffffu;
    state.resource_mark = 0xffffffffu;
    state.layout_resource_entry = 0xffffffffu;
    state.palette_mark = 0xffffffffu;
}

bool load_default_result_layout(
    GameplayResultScreenState& state, u32 result_mode, u32 tribe_index) {
    if (state.resource_mark == 0xffffffffu) {
        state.resource_mark = resource_store_state().next_entry;
    }
    if (state.palette_mark == 0xffffffffu) {
        state.palette_mark = palette_cache_state().next_slot;
    }

    const u32 record_index = kGameplayResultLayoutRecordBase +
        tribe_index * kGameplayResultLayoutTribeStride +
        result_layout_bucket(result_mode) * kGameplayResultLayoutModeStride;
    const u32 palette_slot =
        LoadPaletteCacheTrcRecord(kGameplayResultArchiveName, record_index);
    if (palette_slot == kInvalidPaletteCacheSlot) {
        release_default_result_allocations(state);
        return false;
    }

    const u32 image_entry =
        LoadImageResourceTrcRecord(kGameplayResultArchiveName, record_index + 1);
    if (image_entry == kInvalidResourceEntry ||
        !SetResourceEntryPaletteSlot(image_entry, palette_slot)) {
        release_default_result_allocations(state);
        return false;
    }

    state.layout_resource_entry = image_entry;
    DrawImageResourceNormal(image_entry, 0, 0);
    return true;
}

bool load_default_result_resources(GameplayResultScreenState& state) {
    if (state.resource_mark == 0xffffffffu) {
        state.resource_mark = resource_store_state().next_entry;
    }
    if (state.palette_mark == 0xffffffffu) {
        state.palette_mark = palette_cache_state().next_slot;
    }

    PaletteResourceSequenceResult sequence{};
    if (!LoadPaletteBoundResourceSequence(
            kGameplayResultArchiveName,
            kGameplayResultResourcePaletteRecord,
            kGameplayResultResourceRecordCount,
            &sequence)) {
        release_default_result_allocations(state);
        return false;
    }

    state.resource_base = sequence.resource_start;
    return true;
}

} // namespace

GameplayResultScreenState& gameplay_result_screen_state() {
    return g_gameplay_result_screen_state;
}

bool IsGameplayResultPlayerVisible(const GameplayResultScreenState& state, u32 player_index) {
    if (player_index >= state.players.size()) {
        return false;
    }

    const GameplayResultPlayer& player = state.players[player_index];
    if (player.slot_state == 0x14 || player.slot_state == 2) {
        return false;
    }
    if (player.slot_state < 2 && player.metrics[0] == 0) {
        return false;
    }
    return true;
}

void BuildGameplayResultScoreRows(GameplayResultScreenState& state, u32 player_count) {
    state.player_count = std::min<u32>(player_count,
        static_cast<u32>(kGameplayResultPlayerSlots));
    state.rows.clear();

    for (u32 player = 0; player < state.player_count; ++player) {
        const GameplayResultPlayer& source = state.players[player];
        GameplayResultRow row{};
        row.player_index = player;
        row.faction_id = source.faction_id;
        row.name = source.name;
        row.metrics = source.metrics;
        row.adjustment_score = source.adjustment_score;
        row.visible = IsGameplayResultPlayerVisible(state, player);
        if (!row.visible) {
            continue;
        }

        i32 total = source.total_score;
        if (!source.total_score_valid) {
            total = source.adjustment_score +
                static_cast<i32>(row.metrics[kGameplayResultMetricCount - 1]);
        }
        row.total_score = total;
        state.rows.push_back(row);
    }

    std::sort(state.rows.begin(), state.rows.end(),
        [](const GameplayResultRow& lhs, const GameplayResultRow& rhs) {
            if (lhs.total_score != rhs.total_score) {
                return lhs.total_score > rhs.total_score;
            }
            return lhs.player_index < rhs.player_index;
        });

    state.metric_maxima = metric_maxima_from_rows(state.rows);
    for (GameplayResultRow& row : state.rows) {
        for (std::size_t metric = 0; metric < row.ratios.size(); ++metric) {
            row.ratios[metric] =
                static_cast<float>(row.metrics[metric]) /
                static_cast<float>(safe_metric_max(state.metric_maxima[metric]));
        }
    }
}

void DrawGameplayResultSpriteTokenShadow(GameplayResultScreenState& state, u32 resource_token,
    i32 x, i32 y) {
    if (state.callbacks.draw_sprite != nullptr) {
        state.callbacks.draw_sprite(state, resource_token, x, y);
    }
    else if (state.resource_base != 0xffffffffu) {
        DrawResourceSpriteToken1Shadow(state.resource_base + resource_token, x, y);
    }
    log_result_action(state, GameplayResultActionKind::SpriteDrawn, resource_token);
}

void DrawGameplayResultScoreGraphs(GameplayResultScreenState& state) {
    constexpr i32 kGraphLeft = 0x94;
    constexpr i32 kGraphTop = 0x55;
    constexpr i32 kGraphRowStep = 0x2d;
    constexpr i32 kGraphColumnStep = 0x32;
    constexpr i32 kGraphWideColumnStep = 0x73;
    constexpr i32 kGraphHeight = 0x0b;
    constexpr i32 kGraphWidth = 0x28;
    const bool pixel_mode_555 = SurfacePixelMode555();
    const u16 outline_color = pixel_mode_555 ? 0x0859u : 0x10b9u;
    const u16 fill_color = pixel_mode_555 ? 0x6442u : 0xc8a2u;

    i32 y = kGraphTop;
    for (const GameplayResultRow& row : state.rows) {
        i32 x = kGraphLeft;
        for (std::size_t metric = 0; metric < row.ratios.size() - 1; ++metric) {
            if (state.callbacks.draw_rect != nullptr) {
                state.callbacks.draw_rect(state, x, y, kGraphWidth, kGraphHeight,
                    outline_color, false);
            }
            else {
                DrawBackBufferRectangleOutline16(
                    x, y, kGraphWidth, kGraphHeight, outline_color);
            }

            const i32 filled_width =
                static_cast<i32>(row.ratios[metric] * static_cast<float>(kGraphWidth - 4));
            if (filled_width > 0) {
                if (state.callbacks.draw_rect != nullptr) {
                    state.callbacks.draw_rect(state, x + 2, y + 2, filled_width,
                        kGraphHeight - 4, fill_color, true);
                }
                else {
                    DrawBackBufferFilledRectangle16(x + 2, y + 2, x + 2 + filled_width,
                        y + kGraphHeight - 2, fill_color);
                }
            }

            x += metric == 5 ? kGraphWideColumnStep : kGraphColumnStep;
        }
        y += kGraphRowStep;
    }
    log_result_action(state, GameplayResultActionKind::GraphsDrawn,
        static_cast<u32>(state.rows.size()));
}

void DrawGameplayResultScoreDecorations(GameplayResultScreenState& state) {
    constexpr std::array<std::pair<u32, i32>, 4> kHeaderSprites{{
        {0x0b, 0x96},
        {0x0c, 0x131},
        {0x0d, 0x1f4},
        {0x16, 0x262},
    }};
    constexpr std::array<std::pair<u32, i32>, 7> kRowMetricSprites{{
        {0x0e, 0x96},
        {0x0f, 0xd2},
        {0x10, 0xff},
        {0x11, 0x131},
        {0x12, 0x163},
        {0x13, 0x195},
        {0x14, 0x1fe},
    }};

    for (const auto& [token, x] : kHeaderSprites) {
        DrawGameplayResultSpriteTokenShadow(state, token, x, 0x28);
    }

    i32 y = 0x41;
    for (const GameplayResultRow& row : state.rows) {
        DrawGameplayResultSpriteTokenShadow(
            state, 0x07 + std::min<u32>(row.faction_id, 3), 0x0a, y + 0x14);
        for (const auto& [token, x] : kRowMetricSprites) {
            DrawGameplayResultSpriteTokenShadow(state, token, x, y);
        }
        y += 0x2d;
    }
}

void DrawGameplayResultTextLine(GameplayResultScreenState& state, i32 x, i32 y, u8 color,
    const char* text) {
    if (state.callbacks.draw_text != nullptr) {
        state.callbacks.draw_text(state, x, y, color, text != nullptr ? text : "");
    }
    else {
        draw_result_text_fallback(x, y, color, text);
    }
}

void DrawGameplayResultTotalTextLine(GameplayResultScreenState& state, i32 x, i32 y,
    u8 color, const char* text) {
    if (state.callbacks.draw_text != nullptr) {
        state.callbacks.draw_text(state, x, y, color, text != nullptr ? text : "");
    }
    else {
        draw_result_text_fallback(x, y, color, text);
    }
}

void DrawGameplayResultScoreTable(GameplayResultScreenState& state) {
    constexpr std::array<i32, kGameplayResultMetricCount> kMetricX = {
        0x96, 0xc8, 0xfa, 0x12c, 0x15e, 0x190, 0x1fe, 0x26c,
    };
    i32 y = 0x41;
    for (const GameplayResultRow& row : state.rows) {
        y += 3;
        DrawGameplayResultTextLine(state, 0, y, 1, row.name.c_str());
        y += 0x14;
        for (std::size_t metric = 0; metric < row.metrics.size(); ++metric) {
            draw_metric_text(state, kMetricX[metric], y, row.metrics[metric],
                metric == row.metrics.size() - 1);
        }
        y += 0x16;
    }
    log_result_action(state, GameplayResultActionKind::TableDrawn,
        static_cast<u32>(state.rows.size()));
}

bool LoadGameplayResultScreenResources(GameplayResultScreenState& state, u32 result_mode) {
    state.result_mode = result_mode;
    bool ok = true;
    if (state.callbacks.load_resources != nullptr) {
        ok = state.callbacks.load_resources(state, result_mode, state.selected_tribe_index);
    }
    else {
        ok = load_default_result_resources(state);
    }
    log_result_action(state, GameplayResultActionKind::ResourcesLoaded, result_mode,
        ok ? 1u : 0u);
    return ok;
}

void ReleaseGameplayResultScreenResources(GameplayResultScreenState& state) {
    if (state.callbacks.release_resources != nullptr) {
        state.callbacks.release_resources(state);
    }
    else {
        release_default_result_allocations(state);
    }
    state.resource_base = 0xffffffffu;
    state.resource_mark = 0xffffffffu;
    state.layout_resource_entry = 0xffffffffu;
    state.palette_mark = 0xffffffffu;
    log_result_action(state, GameplayResultActionKind::ResourcesReleased);
}

bool LoadGameplayResultLayoutTemplate(GameplayResultScreenState& state, u32 result_mode,
    u32 tribe_index) {
    bool ok = true;
    if (state.callbacks.load_layout != nullptr) {
        ok = state.callbacks.load_layout(state, result_mode, tribe_index);
    }
    else {
        ok = load_default_result_layout(state, result_mode, tribe_index);
    }
    log_result_action(state, GameplayResultActionKind::LayoutLoaded, result_mode, tribe_index);
    return ok;
}

void RenderGameplayResultRankingScreen(GameplayResultScreenState& state, u32 result_mode,
    u32 player_count) {
    state.result_mode = result_mode;
    if (!LoadGameplayResultLayoutTemplate(state, result_mode, state.selected_tribe_index)) {
        return;
    }
    if (!LoadGameplayResultScreenResources(state, result_mode)) {
        return;
    }

    DrawGameplayResultSpriteTokenShadow(
        state, result_outcome_sprite_token(result_mode), 0, 0);
    BuildGameplayResultScoreRows(state, player_count);
    draw_result_elapsed_time(state);
    DrawGameplayResultScoreDecorations(state);
    DrawGameplayResultScoreGraphs(state);
    DrawGameplayResultScoreTable(state);

    if (state.callbacks.present_cursor != nullptr) {
        state.callbacks.present_cursor(state);
    }
    else {
        present_result_cursor_default();
    }

    ReleaseGameplayResultScreenResources(state);

    if (state.callbacks.set_music_policy_mode != nullptr) {
        state.callbacks.set_music_policy_mode(state, result_mode);
    }
    else {
        set_result_music_policy_default(result_mode);
    }
    log_result_action(state, GameplayResultActionKind::MusicPolicyChanged, result_mode);

    if (state.replay_controls_available && state.replay_record_index_is_zero &&
        !state.scenario_ai_profile_override) {
        if (state.callbacks.open_replay_control_dialog != nullptr) {
            state.callbacks.open_replay_control_dialog(state);
        }
        else {
            open_replay_result_dialog_default();
        }
        log_result_action(state, GameplayResultActionKind::ReplayControlDialogOpened);
    }
    else {
        if (state.callbacks.open_post_result_dialog != nullptr) {
            state.callbacks.open_post_result_dialog(state);
        }
        else {
            open_post_result_dialog_default();
        }
        log_result_action(state, GameplayResultActionKind::PostResultDialogOpened);
    }
}

bool IsGameplayResultPlayerVisible(u32 player_index) {
    return IsGameplayResultPlayerVisible(gameplay_result_screen_state(), player_index);
}

void BuildGameplayResultScoreRows(u32 player_count) {
    BuildGameplayResultScoreRows(gameplay_result_screen_state(), player_count);
}

void DrawGameplayResultSpriteTokenShadow(u32 resource_token, i32 x, i32 y) {
    DrawGameplayResultSpriteTokenShadow(gameplay_result_screen_state(), resource_token, x, y);
}

void DrawGameplayResultScoreGraphs() {
    DrawGameplayResultScoreGraphs(gameplay_result_screen_state());
}

void DrawGameplayResultTextLine(i32 x, i32 y, u8 color, const char* text) {
    DrawGameplayResultTextLine(gameplay_result_screen_state(), x, y, color, text);
}

void DrawGameplayResultTotalTextLine(i32 x, i32 y, u8 color, const char* text) {
    DrawGameplayResultTotalTextLine(gameplay_result_screen_state(), x, y, color, text);
}

void DrawGameplayResultScoreTable() {
    DrawGameplayResultScoreTable(gameplay_result_screen_state());
}

bool LoadGameplayResultScreenResources(u32 result_mode) {
    return LoadGameplayResultScreenResources(gameplay_result_screen_state(), result_mode);
}

void ReleaseGameplayResultScreenResources() {
    ReleaseGameplayResultScreenResources(gameplay_result_screen_state());
}

bool LoadGameplayResultLayoutTemplate(u32 result_mode, u32 tribe_index) {
    return LoadGameplayResultLayoutTemplate(gameplay_result_screen_state(), result_mode,
        tribe_index);
}

void RenderGameplayResultRankingScreen(u32 result_mode, u32 player_count) {
    RenderGameplayResultRankingScreen(gameplay_result_screen_state(), result_mode, player_count);
}

}
