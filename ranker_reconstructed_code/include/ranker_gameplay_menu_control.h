#pragma once

#include "ranker_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kGameplayMenuItemOutline = 0x01;
constexpr u32 kGameplayMenuItemDimWhenIdle = 0x02;
constexpr u32 kGameplayMenuItemShadowSprite = 0x04;
constexpr u32 kGameplayMenuItemClickSound = 0x08;
constexpr u32 kGameplayMenuItemEditableText = 0x10;
constexpr u32 kGameplayMenuItemCommitEditedText = 0x20;
constexpr u32 kGameplayMenuItemActivateOnClick = 0x80;

constexpr u32 kGameplayMenuInputMousePress = 0x01;
constexpr u32 kGameplayMenuInputActivated = 0x02;
constexpr u32 kGameplayMenuInputCancel = 0x04;
constexpr u32 kGameplayMenuInputConfirm = 0x08;
constexpr u32 kGameplayMenuInputText = 0x10;
constexpr u32 kGameplayMenuInputEditing = 0x20;
constexpr u32 kGameplayMenuInputTextChanged = 0x40;

enum class GameplayMenuItemState : u8 {
    Normal = 0,
    Hover = 1,
    Disabled = 2,
};

struct GameplayMenuItem {
    GameplayMenuItemState state = GameplayMenuItemState::Normal;
    u8 flags = 0;
    u16 font_id = 0;
    u32 shadow_sprite_entry = 0;
    u32 alternate_shadow_sprite_entry = 0;
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
    char primary_key = 0;
    char secondary_key = 0;
    std::string text;
    std::string committed_text;
};

struct GameplayMenuInputEvent {
    bool has_event = false;
    bool pointer_changed = false;
    bool mouse_press = false;
    bool mouse_release = false;
    bool quit_requested = false;
    i32 mouse_x = 0;
    i32 mouse_y = 0;
    u8 key_code = 0;
    char text_char = 0;
};

struct GameplayMenuControl;

using GameplayMenuReadInputCallback =
    bool (*)(GameplayMenuControl& control, GameplayMenuInputEvent& event, bool blocking);
using GameplayMenuCallback = void (*)(GameplayMenuControl& control);
using GameplayMenuItemCallback =
    void (*)(GameplayMenuControl& control, GameplayMenuItem& item);
using GameplayMenuDrawTextCallback =
    void (*)(GameplayMenuControl& control, const GameplayMenuItem& item,
        i32 x, i32 y, u8 color, const char* text);

struct GameplayMenuCallbacks {
    GameplayMenuReadInputCallback read_input = nullptr;
    GameplayMenuCallback present_cursor = nullptr;
    GameplayMenuCallback wait_for_cursor_release = nullptr;
    GameplayMenuCallback reset_input = nullptr;
    GameplayMenuCallback click_sound = nullptr;
    GameplayMenuCallback menu_sound = nullptr;
    GameplayMenuCallback text_changed = nullptr;
    GameplayMenuItemCallback draw_shadow_sprite = nullptr;
    GameplayMenuItemCallback dim_item_rect = nullptr;
    GameplayMenuItemCallback draw_outline = nullptr;
    GameplayMenuDrawTextCallback draw_text = nullptr;
};

struct GameplayMenuControl {
    GameplayMenuCallbacks callbacks;
    std::vector<GameplayMenuItem> items;
    std::size_t hover_index = 0;
    std::size_t active_index = 0;
    std::size_t edit_index = 0;
    u32 input_flags = 0;
    u8 normal_color = 1;
    u8 hover_color = 0x41;
    u8 disabled_color = 1;
    bool has_hover = false;
    bool action_pending = false;
    bool single_step_mode = false;
    bool editing = false;
    bool edit_cursor_visible = true;
    bool present_cursor_after_draw = false;
    bool force_redraw_active_item = false;
    u32 redraw_count = 0;
    std::string edit_buffer;
    std::string edit_original_text;
};

void RunGameplayMenuModalLoop(GameplayMenuControl& control, u32 exit_mask = 0);
void PollGameplayMenuModalOnce(GameplayMenuControl& control);
bool HandleGameplayMenuInputWithCursor(GameplayMenuControl& control, bool blocking);
bool HandleGameplayMenuInputNoCursor(GameplayMenuControl& control, bool blocking);
std::size_t FindGameplayMenuItemByKey(GameplayMenuControl& control, char key);
void UpdateGameplayMenuHoverByPoint(GameplayMenuControl& control, i32 x, i32 y);
void InitializeGameplayMenuSelection(GameplayMenuControl& control, i32 x, i32 y);
void RedrawGameplayMenuForHoverChange(GameplayMenuControl& control);
void RedrawGameplayMenuItems(GameplayMenuControl& control);
void DrawGameplayMenuItem(GameplayMenuControl& control, GameplayMenuItem& item);
void DrawGameplayMenuItemAlternate(GameplayMenuControl& control, GameplayMenuItem& item);
void RedrawGameplayMenuItem(GameplayMenuControl& control, std::size_t index);
void RedrawGameplayMenuItemAlternate(GameplayMenuControl& control, std::size_t index);
void AdvanceGameplayMenuCursorRefresh(GameplayMenuControl& control);
void BeginGameplayMenuTextEdit(GameplayMenuControl& control, std::size_t index);
void FinishGameplayMenuTextEdit(GameplayMenuControl& control, bool commit);
bool ApplyGameplayMenuTextEditKey(GameplayMenuControl& control, u8 key_code,
    char text_char);
bool HandleTextEditControlKey(GameplayMenuControl& control, u8 key_code,
    char text_char);

} // namespace ranker
