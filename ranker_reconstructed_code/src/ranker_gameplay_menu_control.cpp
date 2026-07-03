#include "ranker_gameplay_menu_control.h"

#include "ranker_text_renderer.h"
#include "ranker_ui_screen.h"
#include "ranker_unit_movement.h"

#include <algorithm>

namespace ranker {
namespace {

constexpr std::size_t kInvalidMenuIndex = static_cast<std::size_t>(-1);

struct ScopedTextRendererState {
    TextRendererState saved;

    ScopedTextRendererState()
        : saved(text_renderer_state()) {}

    ~ScopedTextRendererState() {
        text_renderer_state() = saved;
    }
};

bool item_enabled(const GameplayMenuItem& item) {
    return item.state != GameplayMenuItemState::Disabled;
}

bool item_contains(const GameplayMenuItem& item, i32 x, i32 y) {
    return x >= item.left && x <= item.right && y >= item.top && y <= item.bottom;
}

u8 item_draw_color(const GameplayMenuControl& control, const GameplayMenuItem& item) {
    if (item.state == GameplayMenuItemState::Hover) {
        return control.hover_color;
    }
    if (item.state == GameplayMenuItemState::Disabled) {
        return control.disabled_color;
    }
    return control.normal_color;
}

std::size_t hover_item_index(const GameplayMenuControl& control) {
    if (!control.has_hover || control.hover_index >= control.items.size()) {
        return kInvalidMenuIndex;
    }
    return control.hover_index;
}

void redraw_if_valid(GameplayMenuControl& control, std::size_t index, bool alternate) {
    if (index >= control.items.size()) {
        return;
    }
    if (alternate) {
        RedrawGameplayMenuItemAlternate(control, index);
    } else {
        RedrawGameplayMenuItem(control, index);
    }
}

bool read_input(GameplayMenuControl& control, GameplayMenuInputEvent& event,
    bool blocking) {
    if (control.callbacks.read_input == nullptr) {
        return false;
    }
    return control.callbacks.read_input(control, event, blocking);
}

void reset_input(GameplayMenuControl& control) {
    if (control.callbacks.reset_input != nullptr) {
        control.callbacks.reset_input(control);
    }
}

void reset_after_menu_activation(GameplayMenuControl& control, bool wait_for_release) {
    if (wait_for_release && control.callbacks.wait_for_cursor_release != nullptr) {
        control.callbacks.wait_for_cursor_release(control);
    }
    reset_input(control);
}

void click_sound(GameplayMenuControl& control) {
    if (control.callbacks.click_sound != nullptr) {
        control.callbacks.click_sound(control);
    }
}

void menu_sound(GameplayMenuControl& control) {
    if (control.callbacks.menu_sound != nullptr) {
        control.callbacks.menu_sound(control);
    }
}

bool handled_item_should_return(const GameplayMenuControl& control) {
    if (!control.has_hover || control.hover_index >= control.items.size()) {
        return false;
    }
    return (control.input_flags & (kGameplayMenuInputActivated |
        kGameplayMenuInputConfirm | kGameplayMenuInputText |
        kGameplayMenuInputEditing | kGameplayMenuInputTextChanged)) != 0;
}

void commit_edit_buffer(GameplayMenuControl& control) {
    if (control.edit_index >= control.items.size()) {
        return;
    }
    GameplayMenuItem& item = control.items[control.edit_index];
    item.text = control.edit_buffer;
    item.committed_text = control.edit_buffer;
    control.input_flags |= kGameplayMenuInputTextChanged;
    if (control.callbacks.text_changed != nullptr) {
        control.callbacks.text_changed(control);
    }
}

void restore_edit_item_pointer(GameplayMenuControl& control) {
    if (control.edit_index >= control.items.size()) {
        return;
    }
    GameplayMenuItem& item = control.items[control.edit_index];
    item.text = control.edit_original_text;
}

void select_item_font(const GameplayMenuItem& item) {
    const u8 font = static_cast<u8>(item.font_id & 0xffu);
    SelectTextDrawFont(font);
    SelectTextMetricFont(font);
}

bool begin_hover_item_edit_or_activate(GameplayMenuControl& control) {
    const std::size_t index = hover_item_index(control);
    if (index == kInvalidMenuIndex) {
        return false;
    }

    GameplayMenuItem& item = control.items[index];
    if ((item.flags & kGameplayMenuItemEditableText) != 0) {
        BeginGameplayMenuTextEdit(control, index);
        control.input_flags = (control.input_flags & ~kGameplayMenuInputActivated) |
            kGameplayMenuInputEditing;
    }
    if ((item.flags & kGameplayMenuItemClickSound) != 0) {
        click_sound(control);
    }
    if ((item.flags & kGameplayMenuItemEditableText) == 0) {
        RedrawGameplayMenuItemAlternate(control, index);
        AdvanceGameplayMenuCursorRefresh(control);
    }
    control.action_pending = false;
    if ((item.flags & kGameplayMenuItemActivateOnClick) != 0) {
        control.input_flags |= kGameplayMenuInputActivated;
        control.active_index = index;
        control.action_pending = true;
        return true;
    }
    return false;
}

void finish_existing_edit_for_new_hover(GameplayMenuControl& control) {
    if (!control.editing) {
        return;
    }
    const bool commit =
        control.edit_index < control.items.size() &&
        (control.items[control.edit_index].flags & kGameplayMenuItemCommitEditedText) != 0;
    FinishGameplayMenuTextEdit(control, commit);
}

bool handle_menu_event(GameplayMenuControl& control, const GameplayMenuInputEvent& event,
    bool present_cursor) {
    if (present_cursor && control.callbacks.present_cursor != nullptr) {
        control.callbacks.present_cursor(control);
    }

    if (event.quit_requested) {
        control.input_flags = kGameplayMenuInputCancel;
        return true;
    }

    if (event.pointer_changed) {
        const std::size_t previous = hover_item_index(control);
        UpdateGameplayMenuHoverByPoint(control, event.mouse_x, event.mouse_y);
        const std::size_t next = hover_item_index(control);
        if (previous != next) {
            RedrawGameplayMenuForHoverChange(control);
        }
    }

    if (!event.has_event) {
        return control.single_step_mode;
    }

    control.input_flags = 0;
    if (event.mouse_press) {
        control.input_flags |= kGameplayMenuInputMousePress;
    }
    if (event.mouse_release) {
        control.input_flags |= kGameplayMenuInputActivated;
    }
    if (event.key_code != 0 || event.text_char != 0) {
        control.input_flags |= kGameplayMenuInputText;
        if (event.key_code == 1) {
            control.input_flags |= kGameplayMenuInputCancel;
        } else if (event.key_code == 0x1c) {
            control.input_flags |= kGameplayMenuInputConfirm;
        }
    }

    if ((control.input_flags & kGameplayMenuInputConfirm) != 0 && control.editing) {
        FinishGameplayMenuTextEdit(control, true);
        control.input_flags = kGameplayMenuInputConfirm;
        return true;
    }

    if ((control.input_flags & kGameplayMenuInputCancel) != 0 && control.editing) {
        FinishGameplayMenuTextEdit(control, false);
        control.input_flags = kGameplayMenuInputCancel;
        return true;
    }

    if ((control.input_flags & kGameplayMenuInputText) != 0 && control.editing) {
        if (ApplyGameplayMenuTextEditKey(control, event.key_code, event.text_char)) {
            RedrawGameplayMenuForHoverChange(control);
        }
        return true;
    }

    if ((control.input_flags & kGameplayMenuInputText) != 0 && !control.editing) {
        const char key = event.text_char != 0 ?
            event.text_char : static_cast<char>(event.key_code);
        const std::size_t index = FindGameplayMenuItemByKey(control, key);
        if (index != kInvalidMenuIndex) {
            finish_existing_edit_for_new_hover(control);
            control.has_hover = true;
            control.hover_index = index;
            control.input_flags = kGameplayMenuInputActivated |
                kGameplayMenuInputTextChanged;
            if (begin_hover_item_edit_or_activate(control)) {
                return true;
            }
            reset_after_menu_activation(control, present_cursor);
            if ((control.input_flags & kGameplayMenuInputTextChanged) != 0) {
                WaitLegacyTickDuration(200);
            }
            redraw_if_valid(control, index, false);
            AdvanceGameplayMenuCursorRefresh(control);
        }
        return control.single_step_mode || index != kInvalidMenuIndex;
    }

    if ((control.input_flags & (kGameplayMenuInputMousePress |
            kGameplayMenuInputActivated)) != 0) {
        const std::size_t index = hover_item_index(control);
        if (index != kInvalidMenuIndex) {
            finish_existing_edit_for_new_hover(control);
            if (begin_hover_item_edit_or_activate(control)) {
                return true;
            }
        }
        reset_after_menu_activation(control, present_cursor);
        if (index != kInvalidMenuIndex) {
            if ((control.input_flags & kGameplayMenuInputTextChanged) != 0) {
                WaitLegacyTickDuration(200);
            }
            menu_sound(control);
            RedrawGameplayMenuItem(control, index);
            AdvanceGameplayMenuCursorRefresh(control);
        }
    }
    return true;
}

} // namespace

void RunGameplayMenuModalLoop(GameplayMenuControl& control, u32 exit_mask) {
    InitializeGameplayMenuSelection(control, 0, 0);
    for (;;) {
        while (!HandleGameplayMenuInputWithCursor(control, true)) {
            if (control.action_pending) {
                return;
            }
        }
        if ((control.input_flags & kGameplayMenuInputCancel) != 0) {
            return;
        }
        if ((control.input_flags & kGameplayMenuInputMousePress) != 0) {
            break;
        }
        if (exit_mask != 0 && (control.input_flags & exit_mask) != 0) {
            return;
        }
        if (control.action_pending || handled_item_should_return(control)) {
            return;
        }
    }
}

void PollGameplayMenuModalOnce(GameplayMenuControl& control) {
    control.single_step_mode = true;
    HandleGameplayMenuInputNoCursor(control, false);
    control.single_step_mode = false;
}

bool HandleGameplayMenuInputWithCursor(GameplayMenuControl& control, bool blocking) {
    GameplayMenuInputEvent event{};
    const bool available = read_input(control, event, blocking);
    if (!available && blocking) {
        return false;
    }
    return handle_menu_event(control, event, true);
}

bool HandleGameplayMenuInputNoCursor(GameplayMenuControl& control, bool blocking) {
    GameplayMenuInputEvent event{};
    const bool available = read_input(control, event, blocking);
    if (!available && blocking) {
        return false;
    }
    return handle_menu_event(control, event, false);
}

std::size_t FindGameplayMenuItemByKey(GameplayMenuControl& control, char key) {
    for (std::size_t i = 0; i < control.items.size(); ++i) {
        GameplayMenuItem& item = control.items[i];
        if (!item_enabled(item)) {
            continue;
        }
        if ((item.primary_key != 0 && item.primary_key == key) ||
            (item.secondary_key != 0 && item.secondary_key == key)) {
            return i;
        }
    }
    return kInvalidMenuIndex;
}

void UpdateGameplayMenuHoverByPoint(GameplayMenuControl& control, i32 x, i32 y) {
    control.has_hover = false;
    for (std::size_t i = 0; i < control.items.size(); ++i) {
        GameplayMenuItem& item = control.items[i];
        if (item.state == GameplayMenuItemState::Disabled) {
            continue;
        }
        if (item_contains(item, x, y)) {
            item.state = GameplayMenuItemState::Hover;
            control.hover_index = i;
            control.has_hover = true;
        } else {
            item.state = GameplayMenuItemState::Normal;
        }
    }
}

void InitializeGameplayMenuSelection(GameplayMenuControl& control, i32 x, i32 y) {
    UpdateGameplayMenuHoverByPoint(control, x, y);
    control.force_redraw_active_item = false;
    RedrawGameplayMenuItems(control);
}

void RedrawGameplayMenuForHoverChange(GameplayMenuControl& control) {
    control.force_redraw_active_item = true;
    RedrawGameplayMenuItems(control);
}

void RedrawGameplayMenuItems(GameplayMenuControl& control) {
    {
        ScopedTextRendererState saved_text;
        for (GameplayMenuItem& item : control.items) {
            select_item_font(item);
            DrawGameplayMenuItem(control, item);
        }
    }
    AdvanceGameplayMenuCursorRefresh(control);
}

void DrawGameplayMenuItem(GameplayMenuControl& control, GameplayMenuItem& item) {
    if ((item.flags & kGameplayMenuItemShadowSprite) != 0 &&
        control.callbacks.draw_shadow_sprite != nullptr) {
        control.callbacks.draw_shadow_sprite(control, item);
    }
    if (!control.force_redraw_active_item &&
        (item.flags & kGameplayMenuItemDimWhenIdle) != 0 &&
        control.callbacks.dim_item_rect != nullptr) {
        control.callbacks.dim_item_rect(control, item);
    }

    const u8 color = item_draw_color(control, item);
    if ((item.flags & kGameplayMenuItemOutline) != 0) {
        if (control.callbacks.draw_outline != nullptr) {
            control.callbacks.draw_outline(control, item);
        } else {
            DrawBackBufferRectangleOutline16(item.left, item.top,
                item.right - item.left, item.bottom - item.top);
        }
    }

    if (control.callbacks.draw_text != nullptr) {
        control.callbacks.draw_text(control, item, item.left + 2, item.top + 2,
            color, item.text.c_str());
    }
}

void DrawGameplayMenuItemAlternate(GameplayMenuControl& control, GameplayMenuItem& item) {
    if (item.alternate_shadow_sprite_entry != 0) {
        const u32 saved = item.shadow_sprite_entry;
        item.shadow_sprite_entry = item.alternate_shadow_sprite_entry;
        DrawGameplayMenuItem(control, item);
        item.shadow_sprite_entry = saved;
        return;
    }
    DrawGameplayMenuItem(control, item);
}

void RedrawGameplayMenuItem(GameplayMenuControl& control, std::size_t index) {
    if (index >= control.items.size()) {
        return;
    }
    ScopedTextRendererState saved_text;
    const bool saved = control.force_redraw_active_item;
    control.force_redraw_active_item = false;
    select_item_font(control.items[index]);
    DrawGameplayMenuItem(control, control.items[index]);
    control.force_redraw_active_item = saved;
}

void RedrawGameplayMenuItemAlternate(GameplayMenuControl& control, std::size_t index) {
    if (index >= control.items.size()) {
        return;
    }
    ScopedTextRendererState saved_text;
    const bool saved = control.force_redraw_active_item;
    control.force_redraw_active_item = false;
    select_item_font(control.items[index]);
    DrawGameplayMenuItemAlternate(control, control.items[index]);
    control.force_redraw_active_item = saved;
}

void AdvanceGameplayMenuCursorRefresh(GameplayMenuControl& control) {
    ++control.redraw_count;
    if (control.present_cursor_after_draw && control.callbacks.present_cursor != nullptr) {
        control.callbacks.present_cursor(control);
    }
}

void BeginGameplayMenuTextEdit(GameplayMenuControl& control, std::size_t index) {
    if (index >= control.items.size()) {
        return;
    }
    GameplayMenuItem& item = control.items[index];
    control.editing = true;
    control.edit_cursor_visible = true;
    control.edit_index = index;
    control.edit_original_text = item.text;
    control.edit_buffer = item.text;
    if (control.edit_buffer.size() < 0x7f) {
        control.edit_buffer.push_back('|');
    }
    item.text = control.edit_buffer;
}

void FinishGameplayMenuTextEdit(GameplayMenuControl& control, bool commit) {
    if (!control.editing) {
        return;
    }
    if (!control.edit_buffer.empty() && control.edit_buffer.back() == '|') {
        control.edit_buffer.pop_back();
    }
    if (commit) {
        commit_edit_buffer(control);
    } else {
        restore_edit_item_pointer(control);
    }
    control.editing = false;
    control.input_flags &= ~kGameplayMenuInputEditing;
}

bool ApplyGameplayMenuTextEditKey(GameplayMenuControl& control, u8 key_code,
    char text_char) {
    if (!control.editing || control.edit_index >= control.items.size()) {
        return false;
    }

    if (!control.edit_buffer.empty() && control.edit_buffer.back() == '|') {
        control.edit_buffer.pop_back();
    }
    if (key_code == 0x0e) {
        if (!control.edit_buffer.empty()) {
            control.edit_buffer.pop_back();
        }
    } else if (text_char != 0 && text_char != '\r' && text_char != '\n') {
        if (control.edit_buffer.size() < 0x7e) {
            control.edit_buffer.push_back(text_char);
        }
    }
    control.edit_buffer.push_back('|');
    control.items[control.edit_index].text = control.edit_buffer;
    return true;
}

bool HandleTextEditControlKey(GameplayMenuControl& control, u8 key_code,
    char text_char) {
    return ApplyGameplayMenuTextEditKey(control, key_code, text_char);
}

} // namespace ranker
