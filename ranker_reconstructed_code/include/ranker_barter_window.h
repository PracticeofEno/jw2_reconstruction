#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"
#include "ranker_network.h"
#include "ranker_raw_indexed_bitmap.h"
#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr UINT kBarterNetworkMessage = 0x465;
constexpr UINT kBarterPromptMessage0 = 0x512;
constexpr UINT kBarterPromptMessage1 = 0x513;
constexpr UINT kBarterPromptMessage2 = 0x514;
constexpr UINT kBarterPromptMessage3 = 0x515;
constexpr UINT kBarterPromptEndMessage = 0x516;

constexpr int kBarterLocalNameEditId = 0x2775;
constexpr int kBarterLocalJemEditId = 0x2776;
constexpr int kBarterInventoryFirstButtonId = 0x2777;
constexpr int kBarterInventorySlotCount = 9;
constexpr int kBarterOfferFirstButtonId = 0x2780;
constexpr int kBarterLocalOfferSlotCount = 4;
constexpr int kBarterRemoteOfferSlotCount = 4;
constexpr int kBarterInsertButtonId = 0x2788;
constexpr int kBarterDeleteButtonId = 0x2789;
constexpr int kBarterLocalOfferJemEditId = 0x278a;
constexpr int kBarterRemoteOfferJemEditId = 0x278b;
constexpr int kBarterOkButtonId = 0x278c;
constexpr int kBarterCancelButtonId = 0x278d;
constexpr int kBarterReadyIndicatorButtonId = 0x278e;
constexpr int kBarterCloseButtonId = 0x278f;
constexpr int kBarterItemInfoPanelId = 0x2790;
constexpr int kBarterFriendInfoPanelId = 0x2791;

constexpr u32 kBarterLayoutTrcRecord = 0x171;
constexpr u32 kBarterBackgroundBitmapRecord = 0x12a;
constexpr u32 kBarterSelectedSlotBitmapRecord = 0x12b;
constexpr u32 kBarterNormalSlotBitmapRecord = 0x12c;
constexpr u32 kBarterInsertNormalBitmapRecord = 0x12d;
constexpr u32 kBarterInsertPressedBitmapRecord = 0x12e;
constexpr u32 kBarterDeleteNormalBitmapRecord = 0x12f;
constexpr u32 kBarterDeletePressedBitmapRecord = 0x130;
constexpr u32 kBarterOkNormalBitmapRecord = 0x131;
constexpr u32 kBarterOkPressedBitmapRecord = 0x132;
constexpr u32 kBarterCancelNormalBitmapRecord = 0x133;
constexpr u32 kBarterCancelPressedBitmapRecord = 0x134;
constexpr u32 kBarterReadyNormalBitmapRecord = 0x135;
constexpr u32 kBarterReadyPressedBitmapRecord = 0x136;
constexpr u32 kBarterCloseNormalBitmapRecord = 0x137;
constexpr u32 kBarterClosePressedBitmapRecord = 0x138;
constexpr int kBarterAcceleratorResourceId = 0x3f2;

struct BarterWindowState;

struct BarterLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct BarterTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct BarterItemDefinition {
    i32 item_id = 0;
    std::string name;
    std::string detail_text;
    i32 category = 0;
    i32 icon_frame = 0;
    i32 tooltip_primary_cost = 0;
    i32 tooltip_secondary_cost = 0;
    i32 hp = 0;
    i32 mp = 0;
    i32 op = 0;
    i32 dp = 0;
    i32 fallback_price = 0;
};

using BarterPacketCallback = void (*)(BarterWindowState& state,
    const void* packet, i32 byte_count);
using BarterPayloadCallback = const u8* (*)(BarterWindowState& state,
    i32& byte_count);
using BarterActionCallback = void (*)(BarterWindowState& state);
using BarterMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color, void* user_data);
using BarterPayloadRouteCallback = void (*)(BarterWindowState& state,
    WPARAM wparam, LPARAM lparam);

struct BarterWindowCallbacks {
    BarterPacketCallback queue_packet = nullptr;
    BarterPayloadCallback receive_payload = nullptr;
    BarterActionCallback play_click_sound = nullptr;
    BarterActionCallback return_to_parent = nullptr;
    BarterActionCallback close_async_socket = nullptr;
    BarterMessageCallback show_message = nullptr;
    BarterPayloadRouteCallback forward_network_message = nullptr;
    void* user_data = nullptr;
};

struct BarterWindowState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    BitmapMemoryResource background;
    BitmapMemoryResource selected_slot_bitmap;
    BitmapMemoryResource normal_slot_bitmap;
    RawIndexedBitmapStrip item_strip;

    BarterTextControl local_name_edit;
    BarterTextControl local_jem_edit;
    BarterTextControl local_offer_jem_edit;
    BarterTextControl remote_offer_jem_edit;

    std::array<LegacyImageButtonControl, kBarterInventorySlotCount> inventory_slots{};
    std::array<LegacyImageButtonControl,
        kBarterLocalOfferSlotCount + kBarterRemoteOfferSlotCount> offer_slots{};
    LegacyImageButtonControl insert_button;
    LegacyImageButtonControl delete_button;
    LegacyImageButtonControl ok_button;
    LegacyImageButtonControl cancel_button;
    LegacyImageButtonControl ready_indicator_button;
    LegacyImageButtonControl close_button;
    LegacyImageButtonControl item_info_panel;
    LegacyImageButtonControl friend_info_panel;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<BarterLayoutRect> layout;
    std::vector<BarterItemDefinition> item_definitions;
    std::array<i32, kBarterInventorySlotCount> inventory{};
    std::array<i32, kBarterLocalOfferSlotCount> local_offer_slots{};
    std::array<i32, kBarterRemoteOfferSlotCount> remote_offer_items{};

    std::array<char, 0x20> local_name{};
    std::array<char, 0x20> remote_name{};
    std::array<char, 0x20> remote_guild{};
    i32 local_jem = 0;
    i32 local_offer_jem = 0;
    i32 remote_offer_jem = 0;
    i32 selected_inventory_slot = 0;
    i32 selected_local_offer_slot = 0;
    i32 current_item_id = -1;
    bool remote_ready = false;
    bool resend_offer_on_timer = false;
    UINT_PTR timer_id = 0;
    BarterWindowCallbacks callbacks{};
};

BarterWindowState& barter_window_state();

void BarterStaticResourceWrapperNN();
#define DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(Name) void Name();
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper00)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper01)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper02)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper03)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper04)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper05)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper06)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper07)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper08)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper09)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper10)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper11)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper12)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper13)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper14)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper15)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper16)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper17)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper18)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper19)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper20)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper21)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper22)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper23)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper24)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper25)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper26)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper27)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper28)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper29)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper30)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper31)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper32)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper33)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper34)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper35)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper36)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper37)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper38)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper39)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper40)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper41)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper42)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper43)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper44)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper45)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper46)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper47)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper48)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper49)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper50)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper51)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper52)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper53)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper54)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper55)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper56)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper57)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper58)
DECLARE_BARTER_STATIC_RESOURCE_WRAPPER(BarterStaticResourceWrapper59)
#undef DECLARE_BARTER_STATIC_RESOURCE_WRAPPER
void InitializeBarterWindowResources(BarterWindowState& state);
void ReleaseBarterWindowResources(BarterWindowState& state);
void InstallBarterWindowAccelerators(BarterWindowState& state);
void RestoreBarterWindowAccelerators(BarterWindowState& state);
bool LoadBarterItemDefinitionsFromJw210Trc(BarterWindowState& state,
    const char* archive_name = "JW2_10.TRC", u32 record_index = 2);
bool CreateBarterWindow(BarterWindowState& state, HWND parent, HINSTANCE instance,
    const char* local_name, const void* remote_summary, std::size_t remote_summary_size,
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr);
void ResetBarterSelection(BarterWindowState& state);
void SetBarterCurrentItem(BarterWindowState& state, i32 item_id);
bool IsBarterInventorySlotAlreadyOffered(const BarterWindowState& state,
    i32 slot);
void SelectBarterInventorySlot(BarterWindowState& state, i32 slot);
void SelectBarterOfferSlot(BarterWindowState& state, i32 slot);
void DispatchBarterNetworkMessage(BarterWindowState& state, WPARAM wparam,
    LPARAM lparam);
LRESULT HandleBarterWindowMessage(BarterWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleBarterControlMessage(BarterWindowState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
