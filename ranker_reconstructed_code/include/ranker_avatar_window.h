#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"
#include "ranker_raw_indexed_bitmap.h"
#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kAvatarNetworkMessage = 0x465;
constexpr UINT kAvatarDeleteAcceptMessage = 0x502;
constexpr UINT kAvatarPromptMessage0 = 0x512;
constexpr UINT kAvatarPromptMessage1 = 0x513;
constexpr UINT kAvatarPromptMessage2 = 0x514;
constexpr UINT kAvatarPromptMessage3 = 0x515;
constexpr UINT kAvatarPromptEndMessage = 0x516;

constexpr int kAvatarDeleteButtonId = 0x2711;
constexpr int kAvatarBuyButtonId = 0x2712;
constexpr int kAvatarSellButtonId = 0x2713;
constexpr int kAvatarEquipButtonId = 0x2714;
constexpr int kAvatarUnequipButtonId = 0x2715;
constexpr int kAvatarCloseButtonId = 0x2716;
constexpr int kAvatarSlotFirstButtonId = 0x2717;
constexpr int kAvatarSlotCount = 8;
constexpr int kAvatarJemEditId = 0x271f;
constexpr int kAvatarNameEditId = 0x2720;
constexpr int kAvatarInfoPanelId = 0x2722;
constexpr int kAvatarEquipmentSlotFirstButtonId = 0x2723;
constexpr int kAvatarEquipmentSlotCount = 6;
constexpr int kAvatarItemInfoPanelId = 0x2729;
constexpr int kAvatarInventorySlotFirstButtonId = 0x272a;
constexpr int kAvatarInventorySlotCount = 9;
constexpr int kAvatarTabBackgroundButtonId = 0x2733;
constexpr int kAvatarAvatarTabButtonId = 0x2734;
constexpr int kAvatarWeaponTabButtonId = 0x2735;
constexpr int kAvatarArmorTabButtonId = 0x2736;
constexpr int kAvatarItemTabButtonId = 0x2737;
constexpr int kAvatarListBoxId = 0x2738;
constexpr int kAvatarScrollBarId = 0x2739;
constexpr int kAvatarBuyNameEditId = 0x273a;
constexpr int kAvatarExpUpButtonId = 0x273b;
constexpr int kAvatarExpPointEditId = 0x273c;

constexpr u32 kAvatarLayoutTrcRecord = 0x170;
constexpr u32 kAvatarTextTrcRecord = 0x15c;
constexpr u32 kAvatarBackgroundBitmapRecord = 0x111;
constexpr u32 kAvatarSelectedSlotBitmapRecord = 0x112;
constexpr u32 kAvatarNormalSlotBitmapRecord = 0x113;
constexpr u32 kAvatarScrollUpBitmapRecord = 0x114;
constexpr u32 kAvatarScrollDownBitmapRecord = 0x115;
constexpr u32 kAvatarScrollTrackBitmapRecord = 0x116;
constexpr u32 kAvatarScrollThumbBitmapRecord = 0x117;
constexpr u32 kAvatarExpUpNormalBitmapRecord = 0x118;
constexpr u32 kAvatarExpUpPressedBitmapRecord = 0x119;
constexpr u32 kAvatarDeleteNormalBitmapRecord = 0x11a;
constexpr u32 kAvatarDeletePressedBitmapRecord = 0x11b;
constexpr u32 kAvatarBuyNormalBitmapRecord = 0x11c;
constexpr u32 kAvatarBuyPressedBitmapRecord = 0x11d;
constexpr u32 kAvatarSellNormalBitmapRecord = 0x11e;
constexpr u32 kAvatarSellPressedBitmapRecord = 0x11f;
constexpr u32 kAvatarEquipNormalBitmapRecord = 0x120;
constexpr u32 kAvatarEquipPressedBitmapRecord = 0x121;
constexpr u32 kAvatarUnequipNormalBitmapRecord = 0x122;
constexpr u32 kAvatarUnequipPressedBitmapRecord = 0x123;
constexpr u32 kAvatarCloseNormalBitmapRecord = 0x124;
constexpr u32 kAvatarClosePressedBitmapRecord = 0x125;
constexpr u32 kAvatarAvatarTabBitmapRecord = 0x126;
constexpr u32 kAvatarWeaponTabBitmapRecord = 0x127;
constexpr u32 kAvatarArmorTabBitmapRecord = 0x128;
constexpr u32 kAvatarItemTabBitmapRecord = 0x129;
constexpr int kAvatarAcceleratorResourceId = 0x3e8;

struct AvatarWindowState;

struct AvatarEquipmentRuntimeVector {
    void* vtable_marker = nullptr;
    void* primary_storage = nullptr;
    void* secondary_storage = nullptr;
    u32 primary_count = 0;
    u32 secondary_count = 0;
    u32 primary_capacity = 0;
    u32 secondary_capacity = 0;
};

struct AvatarLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct AvatarTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct AvatarDefinitionStats {
    std::array<char, 64> display_name{};
    i32 hp = 0;
    i32 mp = 0;
    i32 op = 0;
    i32 dp = 0;
    i32 next_exp_base = 1;
    i32 next_exp_per_level = 0;
};

struct AvatarOwnedSlot {
    std::array<char, 0x14> name{};
    i32 avatar_id = -1;
    i32 hp = 0;
    i32 mp = 0;
    i32 op = 0;
    i32 dp = 0;
    i32 level = 0;
    i32 exp_progress = 0;
    i32 exp_total = 0;
    std::array<i32, kAvatarEquipmentSlotCount> equipment{};
};

struct AvatarCatalogEntry {
    i32 avatar_id = -1;
    i32 price = 0;
    i32 hp = 0;
    i32 mp = 0;
    i32 op = 0;
    i32 dp = 0;
    std::string display_name;
};

struct AvatarItemDefinition {
    i32 item_id = 0;
    std::string name;
    i32 category = 0;
    i32 icon_frame = 0;
    i32 mode = 0;
    i32 hp = 0;
    i32 mp = 0;
    i32 op = 0;
    i32 dp = 0;
    i32 attack = 0;
    i32 defense = 0;
    i32 range = 0;
    i32 movement = 0;
    i32 max_hp = 0;
    i32 max_mp = 0;
    i32 detect = 0;
    i32 cloak = 0;
    i32 exp_bonus = 0;
    i32 level_bonus = 0;
    i32 owner_resource = 0;
    i32 command_value = 0;
    i32 fallback_price = 0;
};

struct AvatarItemOffer {
    bool available = false;
    i32 price = 0;
};

using AvatarPacketCallback = void (*)(AvatarWindowState& state,
    const void* packet, i32 byte_count);
using AvatarPayloadCallback = const u8* (*)(AvatarWindowState& state,
    i32& byte_count);
using AvatarActionCallback = void (*)(AvatarWindowState& state);
using AvatarMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color, void* user_data);
using AvatarDefinitionLookupCallback = bool (*)(AvatarWindowState& state,
    i32 avatar_id, AvatarDefinitionStats& stats);
using AvatarItemCompatibilityCallback = bool (*)(AvatarWindowState& state,
    i32 avatar_slot, i32 item_id);
using AvatarPayloadRouteCallback = void (*)(AvatarWindowState& state,
    WPARAM wparam, LPARAM lparam);

struct AvatarWindowCallbacks {
    AvatarPacketCallback queue_packet = nullptr;
    AvatarPayloadCallback receive_payload = nullptr;
    AvatarActionCallback play_click_sound = nullptr;
    AvatarActionCallback return_to_online_lobby = nullptr;
    AvatarActionCallback close_async_socket = nullptr;
    AvatarMessageCallback show_message = nullptr;
    AvatarDefinitionLookupCallback lookup_avatar_definition = nullptr;
    AvatarItemCompatibilityCallback item_compatible_with_avatar = nullptr;
    AvatarPayloadRouteCallback forward_network_message = nullptr;
    void* user_data = nullptr;
};

struct AvatarWindowState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    BitmapMemoryResource selected_slot_bitmap;
    BitmapMemoryResource normal_slot_bitmap;
    BitmapMemoryResource active_tab_bitmap;
    RawIndexedBitmapStrip avatar_strip;
    RawIndexedBitmapStrip item_strip;
    LegacyCustomScrollControl scroll_bar;

    AvatarTextControl jem_edit;
    AvatarTextControl exp_point_edit;
    AvatarTextControl avatar_name_edit;
    AvatarTextControl buy_name_edit;
    AvatarTextControl list_box;

    LegacyImageButtonControl delete_button;
    LegacyImageButtonControl buy_button;
    LegacyImageButtonControl sell_button;
    LegacyImageButtonControl equip_button;
    LegacyImageButtonControl unequip_button;
    LegacyImageButtonControl close_button;
    LegacyImageButtonControl exp_up_button;
    LegacyImageButtonControl avatar_info_panel;
    LegacyImageButtonControl item_info_panel;
    LegacyImageButtonControl tab_background_button;
    LegacyImageButtonControl avatar_tab_button;
    LegacyImageButtonControl weapon_tab_button;
    LegacyImageButtonControl armor_tab_button;
    LegacyImageButtonControl item_tab_button;
    std::array<LegacyImageButtonControl, kAvatarSlotCount> avatar_slots{};
    std::array<LegacyImageButtonControl, kAvatarEquipmentSlotCount> equipment_slots{};
    std::array<LegacyImageButtonControl, kAvatarInventorySlotCount> inventory_slots{};

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<AvatarLayoutRect> layout;
    std::vector<i32> purchasable_avatar_ids;
    std::vector<AvatarCatalogEntry> avatar_catalog;
    std::vector<AvatarItemDefinition> item_definitions;
    std::vector<AvatarItemOffer> item_offers;
    std::array<AvatarOwnedSlot, kAvatarSlotCount> owned_avatars{};
    std::array<i32, kAvatarInventorySlotCount> inventory{};
    i32 avatar_catalog_version = 0;
    i32 item_catalog_version = 0;
    i32 selected_avatar_slot = 0;
    i32 selected_equipment_slot = 0;
    i32 selected_inventory_slot = 0;
    i32 current_item_id = 0;
    i32 current_tab = 0;
    i32 jem = 0;
    i32 exp_points = 0;
    bool visible = false;
    AvatarWindowCallbacks callbacks{};
};

AvatarWindowState& avatar_window_state();

AvatarEquipmentRuntimeVector& ConstructAvatarEquipmentRuntimeVector(
    AvatarEquipmentRuntimeVector& vector);
void DeleteAvatarEquipmentRuntimeVector(AvatarEquipmentRuntimeVector* vector,
    bool free_storage);
void DestroyAvatarEquipmentRuntimeVector(AvatarEquipmentRuntimeVector& vector);
void InitializeAvatarWindowResources(AvatarWindowState& state);
void ReleaseAvatarWindowResources(AvatarWindowState& state);
#define DECLARE_AVATAR_LIFETIME(StaticName, InitName, RegisterName, DestroyName) \
    void StaticName(AvatarWindowState& state); \
    void InitName(AvatarWindowState& state); \
    void RegisterName(AvatarWindowState& state); \
    void DestroyName(AvatarWindowState& state);

DECLARE_AVATAR_LIFETIME(InitializeAvatarDeleteButtonStatic,
    InitializeAvatarDeleteButton, RegisterAvatarDeleteButtonDestructor,
    DestroyAvatarDeleteButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarBuyButtonStatic,
    InitializeAvatarBuyButton, RegisterAvatarBuyButtonDestructor,
    DestroyAvatarBuyButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarSellButtonStatic,
    InitializeAvatarSellButton, RegisterAvatarSellButtonDestructor,
    DestroyAvatarSellButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarEquipButtonStatic,
    InitializeAvatarEquipButton, RegisterAvatarEquipButtonDestructor,
    DestroyAvatarEquipButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarUnequipButtonStatic,
    InitializeAvatarUnequipButton, RegisterAvatarUnequipButtonDestructor,
    DestroyAvatarUnequipButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarCloseButtonStatic,
    InitializeAvatarCloseButton, RegisterAvatarCloseButtonDestructor,
    DestroyAvatarCloseButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarExpUpButtonStatic,
    InitializeAvatarExpUpButton, RegisterAvatarExpUpButtonDestructor,
    DestroyAvatarExpUpButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarInfoPanelButtonStatic,
    InitializeAvatarInfoPanelButton, RegisterAvatarInfoPanelButtonDestructor,
    DestroyAvatarInfoPanelButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarItemInfoPanelButtonStatic,
    InitializeAvatarItemInfoPanelButton,
    RegisterAvatarItemInfoPanelButtonDestructor,
    DestroyAvatarItemInfoPanelButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarSlotButtonArrayStatic,
    InitializeAvatarSlotButtonArray, RegisterAvatarSlotButtonArrayDestructor,
    DestroyAvatarSlotButtonArray)
DECLARE_AVATAR_LIFETIME(InitializeAvatarEquipmentSlotButtonArrayStatic,
    InitializeAvatarEquipmentSlotButtonArray,
    RegisterAvatarEquipmentSlotButtonArrayDestructor,
    DestroyAvatarEquipmentSlotButtonArray)
DECLARE_AVATAR_LIFETIME(InitializeAvatarInventorySlotButtonArrayStatic,
    InitializeAvatarInventorySlotButtonArray,
    RegisterAvatarInventorySlotButtonArrayDestructor,
    DestroyAvatarInventorySlotButtonArray)
DECLARE_AVATAR_LIFETIME(InitializeAvatarTabBackgroundButtonStatic,
    InitializeAvatarTabBackgroundButton,
    RegisterAvatarTabBackgroundButtonDestructor,
    DestroyAvatarTabBackgroundButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarAvatarTabButtonStatic,
    InitializeAvatarAvatarTabButton, RegisterAvatarAvatarTabButtonDestructor,
    DestroyAvatarAvatarTabButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarWeaponTabButtonStatic,
    InitializeAvatarWeaponTabButton, RegisterAvatarWeaponTabButtonDestructor,
    DestroyAvatarWeaponTabButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarArmorTabButtonStatic,
    InitializeAvatarArmorTabButton, RegisterAvatarArmorTabButtonDestructor,
    DestroyAvatarArmorTabButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarItemTabButtonStatic,
    InitializeAvatarItemTabButton, RegisterAvatarItemTabButtonDestructor,
    DestroyAvatarItemTabButton)
DECLARE_AVATAR_LIFETIME(InitializeAvatarListScrollStatic,
    InitializeAvatarListScroll, RegisterAvatarListScrollDestructor,
    DestroyAvatarListScroll)
DECLARE_AVATAR_LIFETIME(InitializeAvatarActiveTabStatic,
    InitializeAvatarActiveTab, RegisterAvatarActiveTabDestructor,
    DestroyAvatarActiveTab)
DECLARE_AVATAR_LIFETIME(InitializeAvatarSelectedSlotFrameStatic,
    InitializeAvatarSelectedSlotFrame, RegisterAvatarSelectedSlotFrameDestructor,
    DestroyAvatarSelectedSlotFrame)
DECLARE_AVATAR_LIFETIME(InitializeAvatarNormalSlotFrameStatic,
    InitializeAvatarNormalSlotFrame, RegisterAvatarNormalSlotFrameDestructor,
    DestroyAvatarNormalSlotFrame)
DECLARE_AVATAR_LIFETIME(InitializeAvatarIconStripStatic,
    InitializeAvatarIconStrip, RegisterAvatarIconStripDestructor,
    DestroyAvatarIconStrip)
DECLARE_AVATAR_LIFETIME(InitializeAvatarItemIconStripStatic,
    InitializeAvatarItemIconStrip, RegisterAvatarItemIconStripDestructor,
    DestroyAvatarItemIconStrip)
DECLARE_AVATAR_LIFETIME(InitializeAvatarTextTableStatic,
    InitializeAvatarTextTable, RegisterAvatarTextTableDestructor,
    DestroyAvatarTextTable)
DECLARE_AVATAR_LIFETIME(InitializeAvatarEquipmentRuntimeStatic,
    InitializeAvatarEquipmentRuntime, RegisterAvatarEquipmentRuntimeDestructor,
    DestroyAvatarEquipmentRuntime)

#undef DECLARE_AVATAR_LIFETIME
void InstallAvatarWindowAccelerators(AvatarWindowState& state);
void RestoreAvatarWindowAccelerators(AvatarWindowState& state);
bool LoadAvatarItemDefinitionsFromJw210Trc(AvatarWindowState& state,
    const char* archive_name = "JW2_10.TRC", u32 record_index = 2);
std::string BuildAvatarItemStatText(const AvatarItemDefinition& item);
std::string BuildAvatarCatalogStatText(i32 hp, i32 mp, i32 op, i32 dp);
void UpdateAvatarJemText(AvatarWindowState& state);
void UpdateAvatarExpPointText(AvatarWindowState& state);
int CalculateAvatarNextExpThreshold(AvatarWindowState& state,
    const AvatarOwnedSlot& slot);
bool CheckAvatarItemCompatibleWithSelectedAvatar(AvatarWindowState& state,
    int item_id);
i32 GetAvatarCatalogVersion(const AvatarWindowState& state);
i32 GetAvatarItemCatalogVersion(const AvatarWindowState& state);
void ParseAvatarCatalogPacket(AvatarWindowState& state, const u8* payload,
    i32 byte_count);
void ParseAvatarItemOfferPacket(AvatarWindowState& state, const u8* payload,
    i32 byte_count);
bool CreateAvatarWindow(AvatarWindowState& state, HWND parent, HINSTANCE instance,
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr);
void PopulateAvatarList(AvatarWindowState& state, i32 tab);
void SelectAvatarSlot(AvatarWindowState& state, i32 slot);
void SelectAvatarEquipmentSlot(AvatarWindowState& state, i32 slot);
void SelectAvatarInventorySlot(AvatarWindowState& state, i32 slot);
void SetAvatarCurrentItem(AvatarWindowState& state, i32 item_id);
void DispatchAvatarNetworkMessage(AvatarWindowState& state, WPARAM wparam,
    LPARAM lparam);
LRESULT HandleAvatarWindowMessage(AvatarWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleAvatarControlMessage(AvatarWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
