#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"
#include "ranker_raw_indexed_bitmap.h"
#include "ranker_string_selector.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kAccountProfileNetworkMessage = 0x465;
constexpr UINT kAccountProfileReturnMessage = 0x503;
constexpr UINT kAccountProfilePromptMessage0 = 0x512;
constexpr UINT kAccountProfilePromptMessage1 = 0x513;
constexpr UINT kAccountProfilePromptMessage2 = 0x514;
constexpr UINT kAccountProfilePromptMessage3 = 0x515;
constexpr UINT kAccountProfilePromptEndMessage = 0x516;

constexpr int kAccountProfileAccountEditId = 0x157c;
constexpr int kAccountProfilePasswordEditId = 0x157d;
constexpr int kAccountProfileConfirmPasswordEditId = 0x157e;
constexpr int kAccountProfileFocusAccountCommandId = 0x157f;
constexpr int kAccountProfileFocusPasswordCommandId = 0x1580;
constexpr int kAccountProfileFocusConfirmCommandId = 0x1581;
constexpr int kAccountProfileStatusEditId = 0x1582;
constexpr int kAccountProfileIntroEditId = 0x1583;
constexpr int kAccountProfileLocationSelectorId = 0x1584;
constexpr int kAccountProfileBirthYearSelectorId = 0x1585;
constexpr int kAccountProfileSexComboId = 0x1586;
constexpr int kAccountProfileAvatarPowerManButtonId = 0x1587;
constexpr int kAccountProfileAvatarVelocisButtonId = 0x1588;
constexpr int kAccountProfileAvatarRedElfButtonId = 0x1589;
constexpr int kAccountProfileAvatarSkeletonButtonId = 0x158a;
constexpr int kAccountProfileAvatarIconButtonId = 0x158b;
constexpr int kAccountProfileStatsEditId = 0x158c;
constexpr int kAccountProfileForwardFocusCommandId = 0x9c41;
constexpr int kAccountProfileBackwardFocusCommandId = 0x9c42;
constexpr int kAccountProfileOkButtonId = IDOK;
constexpr int kAccountProfileCancelButtonId = IDCANCEL;

constexpr u32 kAccountProfileLayoutTrcRecord = 0x16a;
constexpr u32 kAccountProfileLocationTextTrcRecord = 0x15d;
constexpr u32 kAccountProfileBirthYearTextTrcRecord = 0x15e;
constexpr u32 kAccountProfileSexTextTrcRecord = 0x15f;
constexpr u32 kAccountProfileAvatarTextTrcRecord = 0x160;
constexpr u32 kAccountProfileBackgroundBitmapRecord = 0xb2;
constexpr int kAccountProfileAcceleratorResourceId = 0x226;
constexpr std::size_t kAccountProfileSubmitPacketBytes = 0x9d;

struct AccountProfileState;

struct AccountProfileAvatarStats {
    i32 hp = 0;
    i32 mp = 0;
    i32 op = 0;
    i32 dp = 0;
};

using AccountProfileActionCallback = void (*)(AccountProfileState& state);
using AccountProfilePacketCallback = void (*)(AccountProfileState& state,
    const void* packet, i32 byte_count);
using AccountProfilePayloadCallback = const u8* (*)(AccountProfileState& state,
    i32& byte_count);
using AccountProfileMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);
using AccountProfileAvatarStatsCallback = bool (*)(AccountProfileState& state,
    u32 avatar_record_id, AccountProfileAvatarStats& stats);

struct AccountProfileCallbacks {
    AccountProfileActionCallback open_connect_frontend = nullptr;
    AccountProfileActionCallback open_online_lobby = nullptr;
    AccountProfileActionCallback open_wizard_login = nullptr;
    AccountProfileActionCallback write_setup_data = nullptr;
    AccountProfileActionCallback close_async_socket = nullptr;
    AccountProfilePacketCallback queue_packet = nullptr;
    AccountProfilePayloadCallback receive_payload = nullptr;
    AccountProfileMessageCallback show_message = nullptr;
    AccountProfileAvatarStatsCallback lookup_avatar_stats = nullptr;
};

struct AccountProfileTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct AccountProfileLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct AccountProfileState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    RawIndexedBitmapStrip avatar_strip;
    AccountProfileTextControl account_edit;
    AccountProfileTextControl password_edit;
    AccountProfileTextControl confirm_password_edit;
    AccountProfileTextControl status_edit;
    AccountProfileTextControl intro_edit;
    AccountProfileTextControl stats_edit;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;
    LegacyStringSelectorControl location_selector;
    LegacyStringSelectorControl birth_year_selector;
    LegacyImageComboBoxControl sex_combo;
    std::array<LegacyImageButtonControl, 4> avatar_buttons{};
    LegacyImageButtonControl avatar_icon_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<std::string> location_items;
    std::vector<std::string> birth_year_items;
    std::vector<std::string> sex_items;
    std::array<u32, 4> avatar_record_ids{{1, 34, 17, 49}};
    std::array<std::string, 4> avatar_stat_labels{{"HP: ", "MP: ", "OP: ", "DP: "}};
    std::array<char, 0x20> submitted_account{};
    std::array<char, 0x20> submitted_password{};
    std::array<char, 0x20> submitted_confirm_password{};
    std::array<char, 0x80> submitted_intro{};
    std::vector<u8> last_player_payload;
    std::string last_message;
    u32 selected_avatar_index = 0;
    bool visible = false;
    AccountProfileCallbacks callbacks{};
};

AccountProfileState& account_profile_state();

void InitializeAccountProfileOkButtonStatic(AccountProfileState& state);
void InitializeAccountProfileOkButton(AccountProfileState& state);
void RegisterAccountProfileOkButtonDestructor(AccountProfileState& state);
void DestroyAccountProfileOkButton(AccountProfileState& state);
void InitializeAccountProfileCancelButtonStatic(AccountProfileState& state);
void InitializeAccountProfileCancelButton(AccountProfileState& state);
void RegisterAccountProfileCancelButtonDestructor(AccountProfileState& state);
void DestroyAccountProfileCancelButton(AccountProfileState& state);
void InitializeAccountProfileLocationSelectorStatic(AccountProfileState& state);
void InitializeAccountProfileLocationSelector(AccountProfileState& state);
void RegisterAccountProfileLocationSelectorDestructor(AccountProfileState& state);
void DestroyAccountProfileLocationSelector(AccountProfileState& state);
void InitializeAccountProfileBirthYearSelectorStatic(AccountProfileState& state);
void InitializeAccountProfileBirthYearSelector(AccountProfileState& state);
void RegisterAccountProfileBirthYearSelectorDestructor(AccountProfileState& state);
void DestroyAccountProfileBirthYearSelector(AccountProfileState& state);
void InitializeAccountProfileSexComboBoxStatic(AccountProfileState& state);
void InitializeAccountProfileSexComboBox(AccountProfileState& state);
void RegisterAccountProfileSexComboBoxDestructor(AccountProfileState& state);
void DestroyAccountProfileSexComboBox(AccountProfileState& state);
void InitializeAccountProfilePowerManButtonStatic(AccountProfileState& state);
void InitializeAccountProfilePowerManButton(AccountProfileState& state);
void RegisterAccountProfilePowerManButtonDestructor(AccountProfileState& state);
void DestroyAccountProfilePowerManButton(AccountProfileState& state);
void InitializeAccountProfileVelocisButtonStatic(AccountProfileState& state);
void InitializeAccountProfileVelocisButton(AccountProfileState& state);
void RegisterAccountProfileVelocisButtonDestructor(AccountProfileState& state);
void DestroyAccountProfileVelocisButton(AccountProfileState& state);
void InitializeAccountProfileRedElfButtonStatic(AccountProfileState& state);
void InitializeAccountProfileRedElfButton(AccountProfileState& state);
void RegisterAccountProfileRedElfButtonDestructor(AccountProfileState& state);
void DestroyAccountProfileRedElfButton(AccountProfileState& state);
void InitializeAccountProfileSkeletonButtonStatic(AccountProfileState& state);
void InitializeAccountProfileSkeletonButton(AccountProfileState& state);
void RegisterAccountProfileSkeletonButtonDestructor(AccountProfileState& state);
void DestroyAccountProfileSkeletonButton(AccountProfileState& state);
void InitializeAccountProfileAvatarIconButtonStatic(AccountProfileState& state);
void InitializeAccountProfileAvatarIconButton(AccountProfileState& state);
void RegisterAccountProfileAvatarIconButtonDestructor(AccountProfileState& state);
void DestroyAccountProfileAvatarIconButton(AccountProfileState& state);
void InitializeAccountProfileBackgroundBitmapStatic(AccountProfileState& state);
void InitializeAccountProfileBackgroundBitmap(AccountProfileState& state);
void RegisterAccountProfileBackgroundBitmapDestructor(AccountProfileState& state);
void DestroyAccountProfileBackgroundBitmap(AccountProfileState& state);
void ShutdownAccountProfileBackgroundBitmap(AccountProfileState& state);
void InitializeAccountProfileTextRecordParserStatic(AccountProfileState& state);
void InitializeAccountProfileTextRecordParser(AccountProfileState& state);
void RegisterAccountProfileTextRecordParserDestructor(AccountProfileState& state);
void DestroyAccountProfileTextRecordParser(AccountProfileState& state);
void InitializeAccountProfileAvatarStripStatic(AccountProfileState& state);
void InitializeAccountProfileAvatarStrip(AccountProfileState& state);
void RegisterAccountProfileAvatarStripDestructor(AccountProfileState& state);
void DestroyAccountProfileAvatarStrip(AccountProfileState& state);
void InitializeAccountProfileControls(AccountProfileState& state);
void ReleaseAccountProfileControls(AccountProfileState& state);
void DestroyAccountProfileStringSelector(LegacyStringSelectorControl& selector);

void InstallAccountProfileAccelerators(AccountProfileState& state);
void RestoreAccountProfileAccelerators(AccountProfileState& state);

bool CreateAccountProfileWindow(AccountProfileState& state, HWND parent,
    HINSTANCE instance, LPARAM account_text, LPARAM password_text);
void RefreshAccountProfileAvatarStats(AccountProfileState& state);
bool SubmitAccountProfileRequest(AccountProfileState& state);
void DispatchAccountProfileNetworkMessage(AccountProfileState& state, WPARAM wparam,
    LPARAM lparam);
LRESULT HandleAccountProfileWindowMessage(AccountProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleAccountProfileControlMessage(AccountProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
