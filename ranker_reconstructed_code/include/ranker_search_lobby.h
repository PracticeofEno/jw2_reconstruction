#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_image_controls.h"
#include "ranker_string_selector.h"
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

constexpr UINT kSearchLobbyNetworkMessage = 0x465;
constexpr UINT kSearchLobbyCancelMessage = 0x503;
constexpr UINT kSearchLobbyStatusMessage0 = 0x512;
constexpr UINT kSearchLobbyStatusMessage1 = 0x513;
constexpr UINT kSearchLobbyStatusMessage2 = 0x514;
constexpr UINT kSearchLobbyStatusMessage3 = 0x515;
constexpr UINT kSearchLobbyBusyMessage = 0x516;

constexpr int kSearchLobbyCategoryControlId = 0x28a0;
constexpr int kSearchLobbyBirthYearFromControlId = 0x28a1;
constexpr int kSearchLobbyBirthYearToControlId = 0x28a2;
constexpr int kSearchLobbyRegionControlId = 0x28a3;
constexpr int kSearchLobbyNameListId = 0x28a4;
constexpr int kSearchLobbyCategoryListId = 0x28a5;
constexpr int kSearchLobbyBirthYearListId = 0x28a6;
constexpr int kSearchLobbyRegionListId = 0x28a7;
constexpr int kSearchLobbyFirstPageButtonId = 0x28a8;
constexpr int kSearchLobbyPreviousPageButtonId = 0x28a9;
constexpr int kSearchLobbyNextPageButtonId = 0x28aa;
constexpr int kSearchLobbyCloseButtonId = 0x28ab;
constexpr int kSearchLobbyFocusCommandId = 0x9c41;
constexpr int kSearchLobbyAcceleratorResourceId = 0x410;

constexpr u32 kSearchLobbyLayoutTrcRecord = 0x175;
constexpr u32 kSearchLobbyBackgroundBitmapTrcRecord = 0x149;
constexpr u32 kSearchLobbyBirthYearTextTrcRecord = 0x15e;
constexpr u32 kSearchLobbyRegionTextTrcRecord = 0x15d;
constexpr u32 kSearchLobbyCategoryTextTrcRecord = 0x15f;
constexpr u32 kSearchLobbyCategoryComboBitmapRecord = 0x152;
constexpr u32 kSearchLobbyFirstPageNormalBitmapRecord = 0x14a;
constexpr u32 kSearchLobbyFirstPagePressedBitmapRecord = 0x14b;
constexpr u32 kSearchLobbyPreviousPageNormalBitmapRecord = 0x14c;
constexpr u32 kSearchLobbyPreviousPagePressedBitmapRecord = 0x14d;
constexpr u32 kSearchLobbyNextPageNormalBitmapRecord = 0x14e;
constexpr u32 kSearchLobbyNextPagePressedBitmapRecord = 0x14f;
constexpr u32 kSearchLobbyCloseNormalBitmapRecord = 0x150;
constexpr u32 kSearchLobbyClosePressedBitmapRecord = 0x151;
constexpr std::size_t kSearchLobbyQueryPacketBytes = 0x21;
constexpr std::size_t kSearchLobbySelectedNamePacketBytes = 0x2d;

struct SearchLobbyState;
struct LegacyAsyncTcpSocket;

struct SearchLobbyQuery {
    int page = 0;
    int category_index = 0;
    int birth_year_from = 0;
    int birth_year_to = 0;
    int region_index = 0;
};

struct SearchLobbyResult {
    std::string name;
    std::string category;
    std::string birth_year;
    std::string region;
};

using SearchLobbyPacketCallback = void (*)(SearchLobbyState& state,
    const void* packet, i32 byte_count);

struct SearchLobbyCallbacks {
    void (*send_query)(SearchLobbyState& state, const SearchLobbyQuery& query) = nullptr;
    void (*send_selected_name)(SearchLobbyState& state, const char* name) = nullptr;
    SearchLobbyPacketCallback queue_packet = nullptr;
    void (*handle_network_message)(SearchLobbyState& state, WPARAM wparam,
        LPARAM lparam) = nullptr;
    void (*forward_network_message)(HWND parent, UINT message, WPARAM wparam,
        LPARAM lparam) = nullptr;
    void (*show_message)(HWND owner, const char* text, COLORREF color) = nullptr;
    void (*set_busy)(BOOL busy) = nullptr;
    void (*return_to_parent)(HWND parent, HINSTANCE instance, LPARAM context) = nullptr;
    void (*focus_parent_control)(SearchLobbyState& state) = nullptr;
};

struct SearchLobbyControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct SearchLobbyLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct SearchLobbyState {
    BitmapMemoryResource background;
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;

    LegacyImageComboBoxControl category_control;
    LegacyStringSelectorControl birth_year_from_control;
    LegacyStringSelectorControl birth_year_to_control;
    LegacyStringSelectorControl region_control;
    SearchLobbyControl name_list;
    SearchLobbyControl category_list;
    SearchLobbyControl birth_year_list;
    SearchLobbyControl region_list;
    LegacyImageButtonControl first_page_button;
    LegacyImageButtonControl previous_page_button;
    LegacyImageButtonControl next_page_button;
    LegacyImageButtonControl close_button;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<std::string> category_items;
    std::vector<std::string> birth_year_items;
    std::vector<std::string> region_items;
    std::vector<SearchLobbyResult> results;
    std::array<char, 0x100> selected_name{};
    std::array<char, 0x20> local_account_name{};
    std::string last_status_text;

    int page = 0;
    bool visible = false;
    SearchLobbyCallbacks callbacks;
};

SearchLobbyState& search_lobby_state();

void InitializeSearchLobbySupport(SearchLobbyState& state);
void InitializeSearchLobbyCategoryComboSupport();
void InitializeSearchLobbyCategoryCombo(SearchLobbyState& state);
void RegisterSearchLobbyCategoryComboShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyCategoryCombo(SearchLobbyState& state);
void InitializeSearchLobbyBirthYearFromSelectorSupport();
void InitializeSearchLobbyBirthYearFromSelector(SearchLobbyState& state);
void RegisterSearchLobbyBirthYearFromSelectorShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyBirthYearFromSelector(SearchLobbyState& state);
void InitializeSearchLobbyBirthYearToSelectorSupport();
void InitializeSearchLobbyBirthYearToSelector(SearchLobbyState& state);
void RegisterSearchLobbyBirthYearToSelectorShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyBirthYearToSelector(SearchLobbyState& state);
void InitializeSearchLobbyRegionSelectorSupport();
void InitializeSearchLobbyRegionSelector(SearchLobbyState& state);
void RegisterSearchLobbyRegionSelectorShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyRegionSelector(SearchLobbyState& state);
void InitializeSearchLobbyFirstPageButtonSupport();
void InitializeSearchLobbyFirstPageButton(SearchLobbyState& state);
void RegisterSearchLobbyFirstPageButtonShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyFirstPageButton(SearchLobbyState& state);
void InitializeSearchLobbyPreviousPageButtonSupport();
void InitializeSearchLobbyPreviousPageButton(SearchLobbyState& state);
void RegisterSearchLobbyPreviousPageButtonShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyPreviousPageButton(SearchLobbyState& state);
void InitializeSearchLobbyNextPageButtonSupport();
void InitializeSearchLobbyNextPageButton(SearchLobbyState& state);
void RegisterSearchLobbyNextPageButtonShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyNextPageButton(SearchLobbyState& state);
void InitializeSearchLobbyCloseButtonSupport();
void InitializeSearchLobbyCloseButton(SearchLobbyState& state);
void RegisterSearchLobbyCloseButtonShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyCloseButton(SearchLobbyState& state);
void InitializeSearchLobbyBackgroundSupport();
void InitializeSearchLobbyBackgroundBitmap(SearchLobbyState& state);
void RegisterSearchLobbyBackgroundShutdown(SearchLobbyState& state);
void ShutdownSearchLobbyBackgroundBitmap(SearchLobbyState& state);
void InitializeSearchLobbyAuxiliaryObject0Support();
void InitializeSearchLobbyAuxiliaryObject0(SearchLobbyState& state);
void RegisterSearchLobbyAuxiliaryObject0Shutdown(SearchLobbyState& state);
void ShutdownSearchLobbyAuxiliaryObject0(SearchLobbyState& state);
void InitializeSearchLobbyAuxiliaryObject1Support();
void InitializeSearchLobbyAuxiliaryObject1(SearchLobbyState& state);
void RegisterSearchLobbyAuxiliaryObject1Shutdown(SearchLobbyState& state);
void ShutdownSearchLobbyAuxiliaryObject1(SearchLobbyState& state);
void HandleSearchLobbyNoop();

void InstallSearchLobbyAccelerators(SearchLobbyState& state);
void RestoreSearchLobbyAccelerators(SearchLobbyState& state);

bool CreateSearchLobbyWindow(SearchLobbyState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context);
LRESULT HandleSearchLobbyWindowMessage(SearchLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT HandleSearchLobbyControlMessage(SearchLobbyState& state, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);

SearchLobbyQuery BuildSearchLobbyQuery(const SearchLobbyState& state);
std::array<u8, kSearchLobbyQueryPacketBytes> BuildSearchLobbyQueryPacket(
    const SearchLobbyQuery& query);
std::array<u8, kSearchLobbySelectedNamePacketBytes>
BuildSearchLobbySelectedNamePacket(const char* name);
void ApplySearchLobbyResults(SearchLobbyState& state,
    const std::vector<SearchLobbyResult>& results);
bool ApplySearchLobbyResultPacket(SearchLobbyState& state, const void* packet,
    std::size_t byte_count);
bool DispatchSearchLobbyServerPacket(SearchLobbyState& state, const void* packet,
    std::size_t byte_count);

#endif

}
