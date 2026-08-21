#pragma once

#include "ranker_bitmap_icon_collection.h"
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

constexpr int kPlayerProfileNameEditId = 0x1f41;
constexpr int kPlayerProfileSexEditId = 0x1f42;
constexpr int kPlayerProfileAgeEditId = 0x1f43;
constexpr int kPlayerProfileNormalMeleeEditId = 0x1f44;
constexpr int kPlayerProfileNormalRankEditId = 0x1f45;
constexpr int kPlayerProfileDescriptionEditId = 0x1f46;
constexpr int kPlayerProfileItemDealButtonId = 0x1f47;
constexpr int kPlayerProfileMemoButtonId = 0x1f48;
constexpr int kPlayerProfileGuildIconButtonId = 0x1f49;
constexpr int kPlayerProfileGuildNameEditId = 0x1f4a;
constexpr int kPlayerProfileLocationSelectorId = 0x1f4b;
constexpr int kPlayerProfileAvatarMeleeEditId = 0x1f4c;
constexpr int kPlayerProfileAvatarRankEditId = 0x1f4d;
constexpr int kPlayerProfileAvatarFirstButtonId = 0x1f4e;
constexpr int kPlayerProfileAvatarButtonCount = 8;
constexpr int kPlayerProfileOkButtonId = IDOK;
constexpr int kPlayerProfileCancelButtonId = IDCANCEL;

enum class PlayerProfileWindowPlacement : u8 {
    fullscreen_popup = 0,
    owned_modal_overlay = 1,
};

constexpr PlayerProfileWindowPlacement SelectPlayerProfileWindowPlacement(
    bool has_parent_window) {
    return has_parent_window ? PlayerProfileWindowPlacement::owned_modal_overlay :
        PlayerProfileWindowPlacement::fullscreen_popup;
}

// The simplified profile keeps the normal match records and introduction,
// while the legacy identity, avatar match records, and owned-avatar panels are
// painted back into the stone theme. Keep this classification shared with the
// focused layout regression so removed controls cannot accidentally reappear.
constexpr bool IsPlayerProfileRemovedControlId(int control_id) {
    return control_id == kPlayerProfileNameEditId ||
        control_id == kPlayerProfileSexEditId ||
        control_id == kPlayerProfileAgeEditId ||
        control_id == kPlayerProfileMemoButtonId ||
        control_id == kPlayerProfileGuildIconButtonId ||
        control_id == kPlayerProfileGuildNameEditId ||
        control_id == kPlayerProfileLocationSelectorId ||
        control_id == kPlayerProfileAvatarMeleeEditId ||
        control_id == kPlayerProfileAvatarRankEditId ||
        (control_id >= kPlayerProfileAvatarFirstButtonId &&
            control_id < kPlayerProfileAvatarFirstButtonId +
                kPlayerProfileAvatarButtonCount);
}

constexpr u32 kPlayerProfileLayoutTrcRecord = 0x16d;
constexpr u32 kPlayerProfileAvatarIdTextTrcRecord = 0x15c;
constexpr u32 kPlayerProfileLocationTextTrcRecord = 0x15d;
constexpr u32 kPlayerProfileSexTextTrcRecord = 0x15f;
constexpr u32 kPlayerProfileAvatarInfoTextTrcRecord = 0x160;
constexpr u32 kPlayerProfileBackgroundBitmapRecord = 0xe1;
constexpr u32 kPlayerProfileOkNormalBitmapRecord = 0xe2;
constexpr u32 kPlayerProfileOkPressedBitmapRecord = 0xe3;
constexpr u32 kPlayerProfileCancelNormalBitmapRecord = 0xe4;
constexpr u32 kPlayerProfileCancelPressedBitmapRecord = 0xe5;
constexpr u32 kPlayerProfileItemDealNormalBitmapRecord = 0xe6;
constexpr u32 kPlayerProfileItemDealPressedBitmapRecord = 0xe7;
constexpr u32 kPlayerProfileMemoNormalBitmapRecord = 0xe8;
constexpr u32 kPlayerProfileMemoPressedBitmapRecord = 0xe9;
constexpr int kPlayerProfileAcceleratorResourceId = 0x320;
constexpr std::size_t kPlayerProfilePayloadBytes = 0x1e7;

struct PlayerProfileState;

struct PlayerProfileLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct PlayerProfileTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct PlayerProfilePayload {
    std::array<char, 0x20> name{};
    i32 guild_icon_slot = -1;
    std::array<char, 0x20> guild_name{};
    i32 birth_year = 0;
    i32 sex_index = -1;
    i32 location_index = -1;
    std::array<char, 0x100> description{};
    std::array<i32, 3> normal_melee{};
    i32 normal_rank = -1;
    i32 normal_points = 0;
    std::array<i32, 3> normal_rank_record{};
    std::array<i32, 3> avatar_melee{};
    i32 avatar_rank = -1;
    i32 avatar_points = 0;
    std::array<i32, 3> avatar_rank_record{};
    std::array<i32, kPlayerProfileAvatarButtonCount> avatar_ids{};
    std::array<i32, kPlayerProfileAvatarButtonCount> avatar_levels{};
};

using PlayerProfileActionCallback = void (*)(PlayerProfileState& state);
using PlayerProfilePacketCallback = void (*)(PlayerProfileState& state,
    const void* packet, i32 byte_count);
using PlayerProfileTextCallback = void (*)(PlayerProfileState& state,
    const char* text);

struct PlayerProfileCallbacks {
    PlayerProfilePacketCallback queue_packet = nullptr;
    PlayerProfileActionCallback play_click_sound = nullptr;
    PlayerProfileActionCallback open_item_deal = nullptr;
    PlayerProfileActionCallback focus_online_chat = nullptr;
    PlayerProfileTextCallback write_online_chat_text = nullptr;
};

struct PlayerProfileState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    RawIndexedBitmapStrip avatar_strip;
    BitmapIconResourceCollection local_icons;
    BitmapIconResourceCollection* icon_collection = nullptr;

    PlayerProfileTextControl name_edit;
    PlayerProfileTextControl sex_edit;
    PlayerProfileTextControl age_edit;
    PlayerProfileTextControl normal_melee_edit;
    PlayerProfileTextControl normal_rank_edit;
    PlayerProfileTextControl description_edit;
    PlayerProfileTextControl guild_name_edit;
    PlayerProfileTextControl avatar_melee_edit;
    PlayerProfileTextControl avatar_rank_edit;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;
    LegacyImageButtonControl item_deal_button;
    LegacyImageButtonControl memo_button;
    LegacyImageButtonControl guild_icon_button;
    LegacyStringSelectorControl location_selector;
    std::array<LegacyImageButtonControl, kPlayerProfileAvatarButtonCount> avatar_buttons{};

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<PlayerProfileLayoutRect> layout;
    std::vector<std::string> location_items;
    std::vector<std::string> sex_items;
    std::string avatar_level_label = "Lv. ";
    std::array<i32, 32> avatar_id_list{};
    std::size_t avatar_id_count = 0;

    std::vector<u8> raw_payload;
    PlayerProfilePayload profile;
    std::array<char, 0x20> requested_name{};
    std::array<char, 0x20> local_account_name{};
    HFONT ui_font = nullptr;
    bool parent_disabled_for_modal = false;
    bool own_profile = false;
    bool visible = false;
    PlayerProfileCallbacks callbacks{};
};

PlayerProfileState& player_profile_state();

void InitializePlayerProfileBackgroundStatic(PlayerProfileState& state);
void InitializePlayerProfileBackgroundBitmap(PlayerProfileState& state);
void RegisterPlayerProfileBackgroundDestructor(PlayerProfileState& state);
void DestroyPlayerProfileBackgroundBitmap(PlayerProfileState& state);
void InitializePlayerProfileOkButtonStatic(PlayerProfileState& state);
void InitializePlayerProfileOkButton(PlayerProfileState& state);
void RegisterPlayerProfileOkButtonDestructor(PlayerProfileState& state);
void DestroyPlayerProfileOkButton(PlayerProfileState& state);
void InitializePlayerProfileCancelButtonStatic(PlayerProfileState& state);
void InitializePlayerProfileCancelButton(PlayerProfileState& state);
void RegisterPlayerProfileCancelButtonDestructor(PlayerProfileState& state);
void DestroyPlayerProfileCancelButton(PlayerProfileState& state);
void InitializePlayerProfileItemDealButtonStatic(PlayerProfileState& state);
void InitializePlayerProfileItemDealButton(PlayerProfileState& state);
void RegisterPlayerProfileItemDealButtonDestructor(PlayerProfileState& state);
void DestroyPlayerProfileItemDealButton(PlayerProfileState& state);
void InitializePlayerProfileMemoButtonStatic(PlayerProfileState& state);
void InitializePlayerProfileMemoButton(PlayerProfileState& state);
void RegisterPlayerProfileMemoButtonDestructor(PlayerProfileState& state);
void DestroyPlayerProfileMemoButton(PlayerProfileState& state);
void InitializePlayerProfileGuildIconButtonStatic(PlayerProfileState& state);
void InitializePlayerProfileGuildIconButton(PlayerProfileState& state);
void RegisterPlayerProfileGuildIconButtonDestructor(PlayerProfileState& state);
void DestroyPlayerProfileGuildIconButton(PlayerProfileState& state);
void InitializePlayerProfileLocationSelectorStatic(PlayerProfileState& state);
void InitializePlayerProfileLocationSelector(PlayerProfileState& state);
void RegisterPlayerProfileLocationSelectorDestructor(PlayerProfileState& state);
void DestroyPlayerProfileLocationSelector(PlayerProfileState& state);
void InitializePlayerProfileAvatarButtonVectorStatic(PlayerProfileState& state);
void InitializePlayerProfileAvatarButtonVector(PlayerProfileState& state);
void RegisterPlayerProfileAvatarButtonVectorDestructor(PlayerProfileState& state);
void DestroyPlayerProfileAvatarButtonVector(PlayerProfileState& state);
void InitializePlayerProfileTextRecordParser0Static(PlayerProfileState& state);
void InitializePlayerProfileTextRecordParser0(PlayerProfileState& state);
void RegisterPlayerProfileTextRecordParser0Destructor(PlayerProfileState& state);
void DestroyPlayerProfileTextRecordParser0(PlayerProfileState& state);
void InitializePlayerProfileTextRecordParser1Static(PlayerProfileState& state);
void InitializePlayerProfileTextRecordParser1(PlayerProfileState& state);
void RegisterPlayerProfileTextRecordParser1Destructor(PlayerProfileState& state);
void DestroyPlayerProfileTextRecordParser1(PlayerProfileState& state);
void InitializePlayerProfileAvatarStripStatic(PlayerProfileState& state);
void InitializePlayerProfileAvatarStrip(PlayerProfileState& state);
void RegisterPlayerProfileAvatarStripDestructor(PlayerProfileState& state);
void DestroyPlayerProfileAvatarStrip(PlayerProfileState& state);
void InitializePlayerProfileResources(PlayerProfileState& state);
void ReleasePlayerProfileResources(PlayerProfileState& state);
void InstallPlayerProfileAccelerators(PlayerProfileState& state);
void RestorePlayerProfileAccelerators(PlayerProfileState& state);

bool CreatePlayerProfileWindow(PlayerProfileState& state, HWND parent,
    HINSTANCE instance, const void* profile_payload, std::size_t profile_payload_size,
    const char* requested_name, const char* local_account_name,
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr);
void DrawPlayerProfileAvatarButton(PlayerProfileState& state,
    const DRAWITEMSTRUCT& item, int avatar_index);
LRESULT HandlePlayerProfileWindowMessage(PlayerProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandlePlayerProfileControlMessage(PlayerProfileState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
