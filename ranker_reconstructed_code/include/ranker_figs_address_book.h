#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr int kFigsListBoxId = 0x2134;
constexpr int kFigsNameEditId = 0x2135;
constexpr int kFigsAddressEditId = 0x2136;
constexpr int kFigsFocusNameCommandId = 0x2137;
constexpr int kFigsFocusAddressCommandId = 0x2138;
constexpr int kFigsAddButtonId = 0x2139;
constexpr int kFigsDeleteButtonId = 0x213a;
constexpr int kFigsConnectButtonId = 0x213b;
constexpr int kFigsScrollControlId = 0x213c;
constexpr int kFigsForwardFocusCommandId = 0x9c41;
constexpr int kFigsCancelButtonId = IDCANCEL;

constexpr u32 kFigsLayoutTrcRecord = 0x165;
constexpr u32 kFigsBackgroundBitmapRecord = 0xee;
constexpr u32 kFigsAddNormalBitmapRecord = 0xef;
constexpr u32 kFigsAddPressedBitmapRecord = 0xf0;
constexpr u32 kFigsDeleteNormalBitmapRecord = 0xf1;
constexpr u32 kFigsDeletePressedBitmapRecord = 0xf2;
constexpr u32 kFigsConnectNormalBitmapRecord = 0xf3;
constexpr u32 kFigsConnectPressedBitmapRecord = 0xf4;
constexpr u32 kFigsExitNormalBitmapRecord = 0xf5;
constexpr u32 kFigsExitPressedBitmapRecord = 0xf6;
constexpr int kFigsAcceleratorResourceId = 0x352;
constexpr std::size_t kFigsEntryCount = 50;
constexpr std::size_t kFigsNameBytes = 0x30;
constexpr std::size_t kFigsAddressBytes = 0x50;

struct FigsState;

struct FigsLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct FigsEntry {
    std::array<char, kFigsNameBytes> name{};
    std::array<char, kFigsAddressBytes> address{};
};

struct FigsTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

using FigsActionCallback = void (*)(FigsState& state);
using FigsConnectCallback = void (*)(FigsState& state, const FigsEntry& entry);

struct FigsCallbacks {
    FigsActionCallback play_click_sound = nullptr;
    FigsActionCallback write_setup_data = nullptr;
    FigsActionCallback open_connect_frontend = nullptr;
    FigsConnectCallback connect_to_entry = nullptr;
};

struct FigsState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;

    BitmapMemoryResource background;
    FigsTextControl list_box;
    FigsTextControl name_edit;
    FigsTextControl address_edit;
    LegacyCustomScrollControl scroll;
    LegacyImageButtonControl add_button;
    LegacyImageButtonControl delete_button;
    LegacyImageButtonControl connect_button;
    LegacyImageButtonControl cancel_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<FigsLayoutRect> layout;
    std::array<FigsEntry, kFigsEntryCount> entries{};
    FigsEntry selected_entry{};
    int visible_rows = 12;
    bool visible = false;
    FigsCallbacks callbacks{};
};

FigsState& figs_state();

void InitializeFigsBackgroundStatic(FigsState& state);
void InitializeFigsBackgroundBitmap(FigsState& state);
void RegisterFigsBackgroundDestructor(FigsState& state);
void DestroyFigsBackgroundBitmap(FigsState& state);
void InitializeFigsScrollStatic(FigsState& state);
void InitializeFigsScrollControl(FigsState& state);
void RegisterFigsScrollDestructor(FigsState& state);
void DestroyFigsScrollControl(FigsState& state);
void InitializeFigsAddButtonStatic(FigsState& state);
void InitializeFigsAddButton(FigsState& state);
void RegisterFigsAddButtonDestructor(FigsState& state);
void DestroyFigsAddButton(FigsState& state);
void InitializeFigsDeleteButtonStatic(FigsState& state);
void InitializeFigsDeleteButton(FigsState& state);
void RegisterFigsDeleteButtonDestructor(FigsState& state);
void DestroyFigsDeleteButton(FigsState& state);
void InitializeFigsConnectButtonStatic(FigsState& state);
void InitializeFigsConnectButton(FigsState& state);
void RegisterFigsConnectButtonDestructor(FigsState& state);
void DestroyFigsConnectButton(FigsState& state);
void InitializeFigsExitButtonStatic(FigsState& state);
void InitializeFigsExitButton(FigsState& state);
void RegisterFigsExitButtonDestructor(FigsState& state);
void DestroyFigsExitButton(FigsState& state);
void InitializeFigsResources(FigsState& state);
void ReleaseFigsResources(FigsState& state);
void InstallFigsAccelerators(FigsState& state);
void RestoreFigsAccelerators(FigsState& state);

bool CreateFigsWindow(FigsState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context);
void AppendFigsEntryToListBox(FigsState& state, FigsEntry& entry);
void DeleteFigsListBoxEntry(FigsState& state, i32 list_index);
bool AddFigsAddressEntry(FigsState& state, const char* name, const char* address);
void DeleteSelectedFigsAddressEntry(FigsState& state);
bool AddFigsEntry(FigsState& state, const char* name, const char* address);
void DeleteSelectedFigsEntry(FigsState& state);
void RefreshFigsList(FigsState& state);
LRESULT HandleFigsWindowMessage(FigsState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleFigsControlMessage(FigsState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);

#endif

}
