#pragma once

#include "ranker_bitmap_resource.h"
#include "ranker_custom_scroll.h"
#include "ranker_image_controls.h"
#include "ranker_string_selector.h"
#include "ranker_wizard_login.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <array>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32

constexpr u32 kCreateGameLayoutTrcRecord = 0x164;
constexpr u32 kCreateGameBackgroundBitmapRecord = 0x64;
constexpr u32 kCreateGameScenarioDirBitmapRecord = 0x65;
constexpr u32 kCreateGameComboBitmapRecord = 0x66;
constexpr u32 kCreateGameScrollUpBitmapRecord = 0x67;
constexpr u32 kCreateGameScrollDownBitmapRecord = 0x68;
constexpr u32 kCreateGameScrollThumbBitmapRecord = 0x69;
constexpr u32 kCreateGameScrollTrackBitmapRecord = 0x6a;
constexpr u32 kCreateGameCreateNormalBitmapRecord = 0x6b;
constexpr u32 kCreateGameCreatePressedBitmapRecord = 0x6c;
constexpr u32 kCreateGameCancelNormalBitmapRecord = 0x6d;
constexpr u32 kCreateGameCancelPressedBitmapRecord = 0x6e;
constexpr u32 kCreateGameUpIconBitmapRecord = 0x6f;
constexpr u32 kCreateGameFolderIconBitmapRecord = 0x70;
constexpr u32 kCreateGameGameIconBitmapRecord = 0x71;
constexpr u32 kCreateGameOpenFolderIconBitmapRecord = 0x72;
constexpr u32 kCreateGameAvatarLevelBitmapRecord = 0x73;
constexpr int kCreateGameAcceleratorResourceId = 0x258;

constexpr int kCreateGameNameEditId = 0x1770;
constexpr int kCreateGamePasswordEditId = 0x1771;
constexpr int kCreateGameGameTypeComboId = 0x1772;
constexpr int kCreateGameScreenSizeComboId = 0x1773;
constexpr int kCreateGameMapInfoButtonId = 0x1774;
constexpr int kCreateGameScenarioListId = 0x1775;
constexpr int kCreateGameScrollControlId = 0x1776;
constexpr int kCreateGameScenarioDirButtonId = 0x1777;
constexpr int kCreateGameFocusNameCommandId = 0x1778;
constexpr int kCreateGameFocusPasswordCommandId = 0x1779;
constexpr int kCreateGameFocusGameTypeCommandId = 0x177a;
constexpr int kCreateGameFocusScreenSizeCommandId = 0x177b;
constexpr int kCreateGameCreateButtonId = 0x177c;
constexpr int kCreateGameFocusScenarioListCommandId = 0x177d;
constexpr int kCreateGameActivateScenarioListCommandId = 0x177e;
constexpr int kCreateGameAvatarLevelStartSelectorId = 0x1781;
constexpr int kCreateGameAvatarLevelEndSelectorId = 0x1784;
constexpr int kCreateGameCancelButtonId = IDCANCEL;

constexpr UINT kCreateGameNetworkMessage = 0x465;
constexpr UINT kCreateGameDuplicateNamePromptMessage = 0x511;
constexpr std::size_t kCreateGameNameBytes = 0x14;
constexpr std::size_t kCreateGamePasswordBytes = 10;
constexpr std::size_t kCreateGameSeedPayloadBytes = 0x196;
constexpr std::size_t kCreateGameMapDescriptorBytes = 0x2dc;

struct LegacyAsyncTcpSocket;
struct CreateGameState;

struct CreateGameLayoutRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct CreateGameTextControl {
    HWND window = nullptr;
    WNDPROC original_window_proc = nullptr;
    int id = 0;
};

struct CreateGameScenarioEntry {
    std::array<char, MAX_PATH> name{};
    std::array<char, MAX_PATH> path{};
    DWORD attributes = 0;
    bool directory = false;
    bool parent = false;
};

using CreateGameActionCallback = void (*)(CreateGameState& state);
using CreateGameMessageCallback = void (*)(HWND owner, const char* text,
    COLORREF color);
using CreateGamePacketCallback = void (*)(CreateGameState& state,
    const void* packet, i32 byte_count);

struct CreateGameCallbacks {
    CreateGameActionCallback play_click_sound = nullptr;
    CreateGameActionCallback open_link_lobby = nullptr;
    CreateGameActionCallback open_p2p_lobby = nullptr;
    CreateGameActionCallback open_ipx_lobby = nullptr;
    CreateGameActionCallback open_connect_frontend = nullptr;
    CreateGameActionCallback open_network_ai_lobby = nullptr;
    CreateGameMessageCallback show_message = nullptr;
    CreateGamePacketCallback queue_packet = nullptr;
};

struct CreateGameState {
    HWND window = nullptr;
    HWND parent_window = nullptr;
    HWND main_window = nullptr;
    HINSTANCE instance = nullptr;
    LPARAM return_context = 0;
    LegacyAsyncTcpSocket* async_tcp_socket = nullptr;
    int mode = 0;

    BitmapMemoryResource background;
    BitmapMemoryResource up_icon;
    BitmapMemoryResource folder_icon;
    BitmapMemoryResource game_icon;
    BitmapMemoryResource open_folder_icon;
    BitmapMemoryResource avatar_level_panel;
    CreateGameTextControl name_edit;
    CreateGameTextControl password_edit;
    CreateGameTextControl scenario_list;
    LegacyCustomScrollControl scroll;
    LegacyImageButtonControl scenario_dir_button;
    LegacyImageButtonControl map_info_button;
    LegacyImageButtonControl create_button;
    LegacyImageButtonControl cancel_button;
    LegacyImageComboBoxControl game_type_combo;
    LegacyImageComboBoxControl screen_size_combo;
    LegacyStringSelectorControl avatar_level_start_selector;
    LegacyStringSelectorControl avatar_level_end_selector;

    HACCEL saved_accelerators = nullptr;
    HWND saved_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    HWND active_accelerator_window = nullptr;

    std::vector<CreateGameLayoutRect> layout;
    std::vector<CreateGameScenarioEntry> scenario_entries;
    WizardSessionArchiveDescriptor selected_session;
    std::array<u8, kCreateGameSeedPayloadBytes> session_seed_payload{};
    std::array<u8, kCreateGameMapDescriptorBytes> map_descriptor_payload{};
    std::array<char, kCreateGameNameBytes> game_name{};
    std::array<char, kCreateGamePasswordBytes> password{};
    std::array<char, 0x20> local_player_name{};
    std::array<u32, 3> server_top_bottom_counts{};
    std::array<u32, 5> server_game_type_counts{};
    std::array<u32, 10> server_use_map_counts{};
    std::string base_maps_directory;
    std::string current_directory;
    std::string last_message;
    int visible_rows = 12;
    int selected_entry_index = -1;
    int game_type = 0;
    int screen_size = 0;
    int avatar_level_start = 10;
    int avatar_level_end = 1;
    bool visible = false;
    bool selected_session_valid = false;
    bool avatar_level_controls_visible = false;
    CreateGameCallbacks callbacks{};
};

CreateGameState& create_game_state();

void InitializeCreateGameResources(CreateGameState& state);
void ReleaseCreateGameResources(CreateGameState& state);
void InitializeCreateGameBackgroundStatic(CreateGameState& state);
void InitializeCreateGameBackground(CreateGameState& state);
void RegisterCreateGameBackgroundDestructor(CreateGameState& state);
void DestroyCreateGameBackground(CreateGameState& state);
void InitializeCreateGameUpIconStatic(CreateGameState& state);
void InitializeCreateGameUpIcon(CreateGameState& state);
void RegisterCreateGameUpIconDestructor(CreateGameState& state);
void DestroyCreateGameUpIcon(CreateGameState& state);
void InitializeCreateGameFolderIconStatic(CreateGameState& state);
void InitializeCreateGameFolderIcon(CreateGameState& state);
void RegisterCreateGameFolderIconDestructor(CreateGameState& state);
void DestroyCreateGameFolderIcon(CreateGameState& state);
void InitializeCreateGameOpenFolderIconStatic(CreateGameState& state);
void InitializeCreateGameOpenFolderIcon(CreateGameState& state);
void RegisterCreateGameOpenFolderIconDestructor(CreateGameState& state);
void DestroyCreateGameOpenFolderIcon(CreateGameState& state);
void InitializeCreateGameGameIconStatic(CreateGameState& state);
void InitializeCreateGameGameIcon(CreateGameState& state);
void RegisterCreateGameGameIconDestructor(CreateGameState& state);
void DestroyCreateGameGameIcon(CreateGameState& state);
void InitializeCreateGameGameTypeComboStatic(CreateGameState& state);
void InitializeCreateGameGameTypeCombo(CreateGameState& state);
void RegisterCreateGameGameTypeComboDestructor(CreateGameState& state);
void DestroyCreateGameGameTypeCombo(CreateGameState& state);
void InitializeCreateGameScreenSizeComboStatic(CreateGameState& state);
void InitializeCreateGameScreenSizeCombo(CreateGameState& state);
void RegisterCreateGameScreenSizeComboDestructor(CreateGameState& state);
void DestroyCreateGameScreenSizeCombo(CreateGameState& state);
void InitializeCreateGameScenarioDirButtonStatic(CreateGameState& state);
void InitializeCreateGameScenarioDirButton(CreateGameState& state);
void RegisterCreateGameScenarioDirButtonDestructor(CreateGameState& state);
void DestroyCreateGameScenarioDirButton(CreateGameState& state);
void InitializeCreateGameMapInfoButtonStatic(CreateGameState& state);
void InitializeCreateGameMapInfoButton(CreateGameState& state);
void RegisterCreateGameMapInfoButtonDestructor(CreateGameState& state);
void DestroyCreateGameMapInfoButton(CreateGameState& state);
void InitializeCreateGameCreateButtonStatic(CreateGameState& state);
void InitializeCreateGameCreateButton(CreateGameState& state);
void RegisterCreateGameCreateButtonDestructor(CreateGameState& state);
void DestroyCreateGameCreateButton(CreateGameState& state);
void InitializeCreateGameScenarioListScrollStatic(CreateGameState& state);
void InitializeCreateGameScenarioListScroll(CreateGameState& state);
void RegisterCreateGameScenarioListScrollDestructor(CreateGameState& state);
void DestroyCreateGameScenarioListScroll(CreateGameState& state);
void InitializeCreateGameCancelButtonStatic(CreateGameState& state);
void InitializeCreateGameCancelButton(CreateGameState& state);
void RegisterCreateGameCancelButtonDestructor(CreateGameState& state);
void DestroyCreateGameCancelButton(CreateGameState& state);
void InitializeCreateGameAvatarLevelPanelStatic(CreateGameState& state);
void InitializeCreateGameAvatarLevelPanel(CreateGameState& state);
void RegisterCreateGameAvatarLevelPanelDestructor(CreateGameState& state);
void DestroyCreateGameAvatarLevelPanel(CreateGameState& state);
void InitializeCreateGameAvatarLevelStartSelectorStatic(CreateGameState& state);
void InitializeCreateGameAvatarLevelStartSelector(CreateGameState& state);
void RegisterCreateGameAvatarLevelStartSelectorDestructor(CreateGameState& state);
void DestroyCreateGameAvatarLevelStartSelector(CreateGameState& state);
void InitializeCreateGameAvatarLevelEndSelectorStatic(CreateGameState& state);
void InitializeCreateGameAvatarLevelEndSelector(CreateGameState& state);
void RegisterCreateGameAvatarLevelEndSelectorDestructor(CreateGameState& state);
void DestroyCreateGameAvatarLevelEndSelector(CreateGameState& state);
void InitializeCreateGameSessionDescriptorStatic(CreateGameState& state);
void InitializeCreateGameSessionDescriptor(CreateGameState& state);
void RegisterCreateGameSessionDescriptorDestructor(CreateGameState& state);
void DestroyCreateGameSessionDescriptor(CreateGameState& state);
void DestroyCreateGameSessionArchiveDescriptor(CreateGameState& state);
void InitializeCreateGameSessionArchiveDescriptor(CreateGameState& state);
void InstallCreateGameAccelerators(CreateGameState& state);
void RestoreCreateGameAccelerators(CreateGameState& state);
bool CreateCreateGameWindow(CreateGameState& state, HWND parent, HINSTANCE instance,
    LPARAM return_context, int mode = 0);
void ClearCreateGameScenarioListData(CreateGameState& state);
void PopulateCreateGameScenarioList(CreateGameState& state);
void RefreshCreateGameScenarioList(CreateGameState& state);
bool BrowseCreateGameSelectedDirectory(CreateGameState& state);
bool SubmitCreateGameSelection(CreateGameState& state);
void BuildCreateGameMapDescriptorPayload(CreateGameState& state);
void SetCreateGameAvatarLevelControlsVisible(CreateGameState& state, bool visible);
void UpdateCreateGameAvatarLevelVisibility(CreateGameState& state, bool visible);
void BuildCreateGameSessionSeedPayload(CreateGameState& state,
    const char* game_name, const char* password);
void BuildCreateGameSessionSeedFromFields(CreateGameState& state,
    const char* game_name, const char* password);
void BuildCreateGameSessionSeed(CreateGameState& state);
bool LaunchCreateGameLinkLobby(CreateGameState& state);
LRESULT HandleCreateGameWindowMessage(CreateGameState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);
LRESULT HandleCreateGameControlMessage(CreateGameState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam);

#endif

}
