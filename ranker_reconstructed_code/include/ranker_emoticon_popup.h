#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_bitmap_tile_sheet.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string>

namespace ranker {

#ifdef _WIN32

constexpr UINT kEmoticonPopupAcceptMessage = 0x517;
constexpr UINT kEmoticonPopupAbortMessage = 0x503;
constexpr UINT kEmoticonPopupCommandMessage = 0x465;
constexpr int kEmoticonPopupButtonId = 0x283d;
constexpr int kEmoticonPopupAcceleratorResourceId = 0x406;

struct EmoticonImageButtonControl {
    WNDPROC original_window_proc = nullptr;
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
    BitmapMemoryResource normal_bitmap;
    BitmapMemoryResource pressed_bitmap;
    HWND parent = nullptr;
    HWND window = nullptr;
};

struct EmoticonPopupState {
    EmoticonImageButtonControl button;
    BitmapTileSheetSelector* selector = nullptr;
    HWND popup_window = nullptr;
    HWND owner_dialog = nullptr;
    HWND focus_after_close = nullptr;
    HWND main_window = nullptr;
    HWND active_modal_window = nullptr;
    HINSTANCE instance = nullptr;
    std::string registered_class_name;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;
    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    bool exit_to_main_requested = false;
};

EmoticonPopupState& emoticon_popup_state();

void InitializeEmoticonPopupSupport(EmoticonPopupState& state);
void InitializeEmoticonPopupButton(EmoticonPopupState& state);
void RegisterEmoticonPopupShutdown(EmoticonPopupState& state);
void ShutdownEmoticonPopupButton(EmoticonPopupState& state);
void HandleEmoticonPopupNoop(EmoticonPopupState& state);

void SetEmoticonPopupHostState(EmoticonPopupState& state, HWND main_window,
    HWND owner_dialog, HWND focus_after_close, HACCEL active_accelerators,
    HWND active_accelerator_window);
void InstallEmoticonPopupAccelerators(EmoticonPopupState& state);
void RestoreEmoticonPopupAccelerators(EmoticonPopupState& state);

bool CreateEmoticonPopupWindow(EmoticonPopupState& state, HWND parent,
    HINSTANCE instance, int anchor_x, int anchor_y, const char* class_name,
    BitmapTileSheetSelector& selector);
LRESULT HandleEmoticonPopupMessage(EmoticonPopupState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleEmoticonPopupButtonMessage(EmoticonPopupState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
