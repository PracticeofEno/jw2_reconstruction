#pragma once

#include "ranker_types.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

struct OriginalRoutineRef {
    const char* name;
    u32 address;
    const char* role;
};

constexpr u32 kRankerWinMainAddress = 0x00407250;
constexpr u32 kRankerPeEntryAddress = 0x005257d0;
constexpr std::size_t kRankerReplayPlayerNameCount = 8;
// Ranker_WinMain initializes DAT_0143fff0/01440004/0143ffec to 800x600x16.
// Keep these logical/back-buffer dimensions separate from the presentation
// client size: the original 32-bit process is stretched by cnc-ddraw, whereas
// the reconstructed 64-bit process must perform that presentation step itself.
constexpr int kOriginalClientWidth = 800;
constexpr int kOriginalClientHeight = 600;
constexpr int kOriginalColorDepth = 16;

constexpr bool PresentationCoordinateInsideClient(
    i32 coordinate, i32 presentation_extent) {
    return presentation_extent > 0 &&
        coordinate >= 0 && coordinate < presentation_extent;
}

inline i32 ScalePresentationCoordinateToLogical(
    i32 coordinate, i32 presentation_extent, i32 logical_extent) {
    if (presentation_extent <= 0 || logical_extent <= 0) {
        return 0;
    }
    if (presentation_extent == 1 || logical_extent == 1) {
        return 0;
    }
    // cnc-ddraw 7.1.0 stores mouse.unscale as float, multiplies in float, then
    // calls roundf before its upper clamp.  Do not replace this with exact
    // rational arithmetic: resizable widths such as 777 expose the float32
    // rounding difference at individual hit-test pixels.
    const i32 nonnegative = coordinate < 0 ? 0 : coordinate;
    volatile float unscale = static_cast<float>(logical_extent - 1) /
        static_cast<float>(presentation_extent - 1);
    volatile float scaled = static_cast<float>(nonnegative) * unscale;
    const i32 rounded = static_cast<i32>(std::roundf(scaled));
    return rounded >= logical_extent ? logical_extent - 1 : rounded;
}

const OriginalRoutineRef* winmain_routine_map(std::size_t& count);

bool CpuSupportsMmx();
void WriteStartupTimestampLog(const char* path = "Jw2.log");
bool VerifySetupVersionData();
bool VerifySetupOrFindJw208Archive();

#ifdef _WIN32
bool InitDirectXSubsystems();
void ShutdownDirectXSubsystems();
bool StartBackgroundWorkerThread();
bool RaiseMainThreadPriority();
void YieldBackgroundWorkerThreadSlice();
void TerminateBackgroundWorkerThread();
void ExitBackgroundWorkerThread();
void ClearActiveAcceleratorState();
void SetActiveAcceleratorState(HWND window, HACCEL accelerators);
void NoOpFrontendNetworkPayloadHandler(const void* packet, u32 byte_count);
void QueueFrontendNetworkChatPacketDisplay(const void* packet, u32 byte_count);
bool QueryRegistryValueBytes(HKEY root, const char* subkey, const char* value_name,
    DWORD* type, BYTE* data, DWORD* byte_count);
void InitializeRuntimeClockSnapshot();
void NoOpStartupRuntimeHook();

using RankerMainWindowVoidCallback = void (*)(void* user_data);
using RankerMainWindowLaunchCallback = bool (*)(
    HWND window, const char* target, const char* parameters, void* user_data);
using RankerMainWindowStatusCallback = void (*)(
    HWND window, const char* text, u32 color, void* user_data);
using RankerMainWindowFrontendModeCallback = void (*)(
    HWND window, HINSTANCE instance, u32 mode, void* user_data);
using RankerMainWindowReplayCallback = void (*)(
    HWND window, HINSTANCE instance, void* user_data);
using RankerMainWindowRouteCallback = void (*)(
    HWND window, WPARAM wparam, LPARAM lparam, void* user_data);

struct RankerMainWindowCallbacks {
    void* user_data = nullptr;
    RankerMainWindowVoidCallback suspend_worker_thread = nullptr;
    RankerMainWindowVoidCallback resume_worker_thread = nullptr;
    RankerMainWindowLaunchCallback launch_external = nullptr;
    RankerMainWindowStatusCallback show_status_message = nullptr;
    RankerMainWindowVoidCallback start_legacy_udp_mode1_receive_thread = nullptr;
    RankerMainWindowFrontendModeCallback open_multiplayer_frontend = nullptr;
    RankerMainWindowFrontendModeCallback open_game_frontend_modal = nullptr;
    RankerMainWindowReplayCallback open_replay_save_dialog = nullptr;
    RankerMainWindowReplayCallback open_replay_load_dialog = nullptr;
    RankerMainWindowRouteCallback route_frontend_message = nullptr;
    RankerMainWindowRouteCallback emit_generic_ai_profile_message = nullptr;
};

struct RankerMainWindowStateSnapshot {
    HWND main_window = nullptr;
    HWND frontend_route_window = nullptr;
    HINSTANCE instance = nullptr;
    const char* external_launch_target = nullptr;
    const char* last_external_launch_target = nullptr;
    const char* last_external_launch_parameters = nullptr;
    std::intptr_t last_external_launch_result = 0;
    u32 frontend_mode = 0;
    bool windowed_mode = false;
    bool app_active = false;
    bool input_enabled = true;
    bool last_external_launch_succeeded = false;
    bool worker_paused = false;
    bool message_wait_worker_suspend_enabled = false;
    bool worker_thread_started = false;
    bool worker_thread_running = false;
    bool suppress_paint = false;
    HWND active_accelerator_window = nullptr;
    HACCEL active_accelerators = nullptr;
    std::size_t frontend_network_chat_message_count = 0;
};

void SetRankerMainWindowCallbacks(const RankerMainWindowCallbacks& callbacks);
void SetRankerMainWindowExternalLaunchTarget(const char* target);
void SetRankerMainWindowFrontendMode(u32 mode);
void SetRankerMainWindowFrontendRouteWindow(HWND window);
void SetRankerMainWindowGenericAiProfileState(bool enabled, bool scenario_active);
void SetRankerMainWindowScenarioAiProfileOverride(bool enabled);
void SetRankerMainWindowNetworkAiProfileOverride(bool enabled);
std::array<std::string, kRankerReplayPlayerNameCount> RankerMainWindowReplayPlayerNames();
RankerMainWindowStateSnapshot RankerMainWindowState();
POINT RankerFrontendWindowOrigin();
POINT RankerCenteredFrontendWindowOrigin(int width, int height);
const std::vector<std::string>& FrontendNetworkChatMessages();
LRESULT CALLBACK HandleRankerMainWindowMessage(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void RouteMainWindowFrontendNetworkMessage(HWND window, WPARAM wparam, LPARAM lparam);
void OpenMultiplayerFrontendForActiveMode(HWND window);
void EnterHostedOrJoinedP2PGameFlow(HWND window, HINSTANCE instance);
bool StartRankerFrontendStageFromMenu(i32 column, i32 row);
void CompleteRankerFrontendStage(u32 result, u32 next_mode);
void SendMainWindowCloseMessage();
void RebuildUnitSpatialIndexes();

int run_reconstructed_winmain(HINSTANCE instance, LPSTR command_line, int show_command);
#else
int run_reconstructed_winmain();
#endif

}
