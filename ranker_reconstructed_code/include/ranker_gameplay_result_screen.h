#pragma once

#include "ranker_types.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

constexpr std::size_t kGameplayResultPlayerSlots = 8;
constexpr std::size_t kGameplayResultMetricCount = 8;
constexpr const char* kGameplayResultArchiveName = "JW2_02.TRC";
// FUN_00430e20 passes JW2_02.TRC record 0x34 as the palette/start record and
// 0x1a as the total record count to FUN_004326c0.
constexpr u32 kGameplayResultResourcePaletteRecord = 0x34;
constexpr u32 kGameplayResultResourceRecordCount = 0x1a;
constexpr u32 kGameplayResultLayoutRecordBase = 0x4e;
constexpr u32 kGameplayResultLayoutTribeStride = 4;
constexpr u32 kGameplayResultLayoutModeStride = 2;

enum class GameplayResultActionKind : u32 {
    None = 0,
    LayoutLoaded,
    ResourcesLoaded,
    ResourcesReleased,
    SpriteDrawn,
    GraphsDrawn,
    TableDrawn,
    PostResultDialogOpened,
    ReplayControlDialogOpened,
    MusicPolicyChanged,
};

struct GameplayResultScreenState;

using GameplayResultCallback = void (*)(GameplayResultScreenState& state);
using GameplayResultLoadCallback =
    bool (*)(GameplayResultScreenState& state, u32 result_mode, u32 tribe_index);
using GameplayResultValueCallback = void (*)(GameplayResultScreenState& state, u32 value);
using GameplayResultTextCallback =
    void (*)(GameplayResultScreenState& state, i32 x, i32 y, u8 color, const char* text);
using GameplayResultSpriteCallback =
    void (*)(GameplayResultScreenState& state, u32 resource_token, i32 x, i32 y);
using GameplayResultRectCallback = void (*)(GameplayResultScreenState& state, i32 left,
    i32 top, i32 width, i32 height, u16 color, bool filled);

struct GameplayResultCallbacks {
    GameplayResultLoadCallback load_layout = nullptr;
    GameplayResultLoadCallback load_resources = nullptr;
    GameplayResultCallback release_resources = nullptr;
    GameplayResultSpriteCallback draw_sprite = nullptr;
    GameplayResultRectCallback draw_rect = nullptr;
    GameplayResultTextCallback draw_text = nullptr;
    GameplayResultCallback present_cursor = nullptr;
    GameplayResultCallback open_post_result_dialog = nullptr;
    GameplayResultCallback open_replay_control_dialog = nullptr;
    GameplayResultValueCallback set_music_policy_mode = nullptr;
};

struct GameplayResultPlayer {
    std::string name;
    u8 slot_state = 0;
    u32 owner_id = 0;
    u32 faction_id = 0;
    std::array<u32, kGameplayResultMetricCount> metrics{};
    i32 adjustment_score = 0;
    i32 total_score = 0;
    bool total_score_valid = false;
    bool connected = true;
};

struct GameplayResultRow {
    u32 player_index = 0;
    u32 faction_id = 0;
    std::string name;
    std::array<u32, kGameplayResultMetricCount> metrics{};
    std::array<float, kGameplayResultMetricCount> ratios{};
    i32 adjustment_score = 0;
    i32 total_score = 0;
    bool visible = false;
};

struct GameplayResultAction {
    GameplayResultActionKind kind = GameplayResultActionKind::None;
    u32 value0 = 0;
    u32 value1 = 0;
    std::string text;
};

struct GameplayResultScreenState {
    GameplayResultCallbacks callbacks;
    std::array<GameplayResultPlayer, kGameplayResultPlayerSlots> players;
    std::array<u32, kGameplayResultMetricCount> metric_maxima{};
    std::vector<GameplayResultRow> rows;
    std::vector<GameplayResultAction> action_log;

    u32 selected_tribe_index = 0;
    u32 result_mode = 0;
    u32 player_count = 0;
    u32 resource_base = 0xffffffffu;
    u32 resource_mark = 0xffffffffu;
    u32 layout_resource_entry = 0xffffffffu;
    u32 palette_mark = 0xffffffffu;
    u32 elapsed_seconds = 0;
    u32 elapsed_minutes = 0;
    u32 elapsed_hours = 0;
    bool generic_ai_profile_mode = false;
    bool scenario_ai_profile_override = false;
    bool replay_controls_available = false;
    bool replay_record_index_is_zero = false;
};

GameplayResultScreenState& gameplay_result_screen_state();

bool IsGameplayResultPlayerVisible(const GameplayResultScreenState& state, u32 player_index);
void BuildGameplayResultScoreRows(GameplayResultScreenState& state, u32 player_count);
void DrawGameplayResultSpriteTokenShadow(GameplayResultScreenState& state, u32 resource_token,
    i32 x = 0, i32 y = 0);
void DrawGameplayResultScoreGraphs(GameplayResultScreenState& state);
void DrawGameplayResultTextLine(GameplayResultScreenState& state, i32 x, i32 y, u8 color,
    const char* text);
void DrawGameplayResultTotalTextLine(GameplayResultScreenState& state, i32 x, i32 y, u8 color,
    const char* text);
void DrawGameplayResultScoreTable(GameplayResultScreenState& state);
bool LoadGameplayResultScreenResources(GameplayResultScreenState& state, u32 result_mode);
void ReleaseGameplayResultScreenResources(GameplayResultScreenState& state);
bool LoadGameplayResultLayoutTemplate(GameplayResultScreenState& state, u32 result_mode,
    u32 tribe_index);
void RenderGameplayResultRankingScreen(GameplayResultScreenState& state, u32 result_mode,
    u32 player_count);

bool IsGameplayResultPlayerVisible(u32 player_index);
void BuildGameplayResultScoreRows(u32 player_count);
void DrawGameplayResultSpriteTokenShadow(u32 resource_token, i32 x = 0, i32 y = 0);
void DrawGameplayResultScoreGraphs();
void DrawGameplayResultTextLine(i32 x, i32 y, u8 color, const char* text);
void DrawGameplayResultTotalTextLine(i32 x, i32 y, u8 color, const char* text);
void DrawGameplayResultScoreTable();
bool LoadGameplayResultScreenResources(u32 result_mode);
void ReleaseGameplayResultScreenResources();
bool LoadGameplayResultLayoutTemplate(u32 result_mode, u32 tribe_index);
void RenderGameplayResultRankingScreen(u32 result_mode, u32 player_count);

}
