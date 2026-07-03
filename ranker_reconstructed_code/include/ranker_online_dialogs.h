#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"
#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string>

namespace ranker {

#ifdef _WIN32

constexpr UINT kOnlinePromptAcceptMessage = 0x502;
constexpr UINT kOnlinePromptCancelMessage = 0x503;
constexpr UINT kOnlinePromptStatusMessage0 = 0x512;
constexpr UINT kOnlinePromptStatusMessage1 = 0x513;
constexpr UINT kOnlinePromptStatusMessage2 = 0x514;
constexpr UINT kOnlinePromptStatusMessage3 = 0x515;
constexpr UINT kOnlinePromptEndMessage = 0x516;
constexpr int kOnlinePromptOkButtonId = 0x3b7;
constexpr int kOnlinePromptCancelButtonId = 0x3b8;
constexpr int kOnlinePromptTextControlId = 0x3ab;
constexpr int kOnlinePromptModalOkButtonId = 0x3ac;
constexpr int kOnlinePromptModalCancelButtonId = 0x3ad;
constexpr u32 kOnlinePromptBackgroundRecord = 0xad;
constexpr u32 kOnlinePromptOkNormalRecord = 0xae;
constexpr u32 kOnlinePromptOkPressedRecord = 0xaf;
constexpr u32 kOnlinePromptCancelNormalRecord = 0xb0;
constexpr u32 kOnlinePromptCancelPressedRecord = 0xb1;

struct OnlineModelessPromptState {
    BitmapMemoryResource background;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;
    HWND window = nullptr;
    HWND owner = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    WPARAM accept_wparam = 0;
    LPARAM accept_lparam = 0;
    COLORREF text_color = RGB(255, 255, 255);
    bool two_buttons = false;
    std::string text;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;
};

struct OnlineModalPromptState {
    BitmapMemoryResource background;
    BitmapMemoryResource ok_normal;
    BitmapMemoryResource ok_pressed;
    BitmapMemoryResource cancel_normal;
    BitmapMemoryResource cancel_pressed;
    HWND owner = nullptr;
    HWND dialog = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    std::string text;
    COLORREF text_color = RGB(255, 255, 255);
    INT_PTR result = 0;
    bool synthetic_template = false;
};

OnlineModelessPromptState& online_modeless_prompt_state();
OnlineModalPromptState& online_modal_prompt_state();

void InitializeOnlineModelessPromptBackgroundStatic(
    OnlineModelessPromptState& state);
void InitializeOnlineModelessPromptBackground(OnlineModelessPromptState& state);
void RegisterOnlineModelessPromptBackgroundDestructor(
    OnlineModelessPromptState& state);
void DestroyOnlineModelessPromptBackground(OnlineModelessPromptState& state);
void InitializeOnlineModelessPromptOkButtonStatic(OnlineModelessPromptState& state);
void InitializeOnlineModelessPromptOkButton(OnlineModelessPromptState& state);
void RegisterOnlineModelessPromptOkButtonDestructor(
    OnlineModelessPromptState& state);
void DestroyOnlineModelessPromptOkButton(OnlineModelessPromptState& state);
void InitializeOnlineModelessPromptCancelButtonStatic(
    OnlineModelessPromptState& state);
void InitializeOnlineModelessPromptCancelButton(OnlineModelessPromptState& state);
void RegisterOnlineModelessPromptCancelButtonDestructor(
    OnlineModelessPromptState& state);
void DestroyOnlineModelessPromptCancelButton(OnlineModelessPromptState& state);
void InitializeOnlineModelessPromptButtons(OnlineModelessPromptState& state);
void DestroyOnlineModelessPromptButtons(OnlineModelessPromptState& state);
void IgnoreOnlineModelessPromptReservedHelper();
void InstallOnlineModelessPromptAcceleratorTarget(OnlineModelessPromptState& state);
void RestoreOnlineModelessPromptAcceleratorTarget(OnlineModelessPromptState& state);
bool CreateOnlineModelessPrompt(OnlineModelessPromptState& state, HWND owner,
    HINSTANCE instance, const char* text, COLORREF text_color, bool two_buttons,
    WPARAM accept_wparam, LPARAM accept_lparam);
LRESULT HandleOnlineModelessPromptMessage(OnlineModelessPromptState& state,
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleOnlineModelessPromptButtonMessage(OnlineModelessPromptState& state,
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

void InitializeOnlineModalPromptBackgroundStatic(OnlineModalPromptState& state);
void InitializeOnlineModalPromptBackground(OnlineModalPromptState& state);
void RegisterOnlineModalPromptBackgroundDestructor(OnlineModalPromptState& state);
void DestroyOnlineModalPromptBackground(OnlineModalPromptState& state);
void InitializeOnlineModalPromptOkNormalStatic(OnlineModalPromptState& state);
void InitializeOnlineModalPromptOkNormal(OnlineModalPromptState& state);
void RegisterOnlineModalPromptOkNormalDestructor(OnlineModalPromptState& state);
void DestroyOnlineModalPromptOkNormal(OnlineModalPromptState& state);
void InitializeOnlineModalPromptOkPressedStatic(OnlineModalPromptState& state);
void InitializeOnlineModalPromptOkPressed(OnlineModalPromptState& state);
void RegisterOnlineModalPromptOkPressedDestructor(OnlineModalPromptState& state);
void DestroyOnlineModalPromptOkPressed(OnlineModalPromptState& state);
void InitializeOnlineModalPromptCancelNormalStatic(OnlineModalPromptState& state);
void InitializeOnlineModalPromptCancelNormal(OnlineModalPromptState& state);
void RegisterOnlineModalPromptCancelNormalDestructor(
    OnlineModalPromptState& state);
void DestroyOnlineModalPromptCancelNormal(OnlineModalPromptState& state);
void InitializeOnlineModalPromptCancelPressedStatic(OnlineModalPromptState& state);
void InitializeOnlineModalPromptCancelPressed(OnlineModalPromptState& state);
void RegisterOnlineModalPromptCancelPressedDestructor(
    OnlineModalPromptState& state);
void DestroyOnlineModalPromptCancelPressed(OnlineModalPromptState& state);
void InitializeOnlineModalPromptResources(OnlineModalPromptState& state);
void DestroyOnlineModalPromptResources(OnlineModalPromptState& state);
void LoadOnlineModalPromptResources(OnlineModalPromptState& state);
INT_PTR ShowOnlineModalPromptResource(OnlineModalPromptState& state, HWND owner,
    HINSTANCE instance, int resource_id, const char* text, COLORREF text_color);
INT_PTR ShowOnlineModalPrompt0(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color);
INT_PTR ShowOnlineModalPrompt1(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color);
INT_PTR ShowOnlineModalPrompt2(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color);
INT_PTR ShowOnlineModalPrompt3(OnlineModalPromptState& state, HWND owner,
    const char* text, COLORREF text_color);
void EndOnlineModalPrompt(OnlineModalPromptState& state, INT_PTR result);
void UpdateOnlineModalPromptText(OnlineModalPromptState& state, const char* text);
INT_PTR CALLBACK HandleOnlineModalPromptDialogMessage(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);

#endif

}
