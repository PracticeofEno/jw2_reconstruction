#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <dsound.h>
#endif

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32
struct SpriteRenderTarget;
struct TrcRecordReader;

struct DirectDrawRuntimeState {
    LPDIRECTDRAW7 direct_draw = nullptr;
    LPDIRECTDRAWSURFACE7 primary_surface = nullptr;
    LPDIRECTDRAWSURFACE7 back_surface = nullptr;
    LPDIRECTDRAWCLIPPER clipper = nullptr;
    std::array<LPDIRECTDRAWSURFACE7, 8> surface_snapshots{};
    RECT client_rect{};
    RECT screen_rect{};
    RECT region_copy_rect{};
    POINT region_copy_destination{};
    HRESULT last_result = DD_OK;
    std::string last_error_name;
    u32 width = 800;
    u32 height = 600;
    u32 color_depth = 16;
    u32 red_mask = 0;
    u32 green_mask = 0;
    u32 blue_mask = 0;
    u32 pixel_mode_555 = 0;
    u32 row_stride_words = 0;
    u32 surface_snapshot_depth = 0;
    std::vector<u32> scanline_offsets;
    bool active = false;
    bool windowed = true;
    bool region_copy_rect_set = false;
};

struct DirectSoundRuntimeState {
    LPDIRECTSOUND direct_sound = nullptr;
    LPDIRECTSOUNDBUFFER primary_buffer = nullptr;
    std::vector<LPDIRECTSOUNDBUFFER> secondary_buffers;
    std::array<LPDIRECTSOUNDBUFFER, 24> reserved_buffers{};
    HRESULT last_result = DS_OK;
    DWORD average_bytes_per_second = 0;
    DWORD last_status = 0;
    DWORD last_frequency = 0;
    LONG last_volume = 0;
    LONG last_pan = 0;
    DWORD play_flags = 0;
    DWORD secondary_buffer_extra_flags = 0;
    LONG volume_delta = 0;
    LONG pan_delta = 0;
    u32 current_slot_index = 0;
    u32 reserved_buffer_scan_index = 0;
    u32 next_allocated_slot = 0;
    bool active = false;
};

struct BinkVideoRuntimeState {
    std::string archive_name;
    std::string record_name;
    u32 record_index = 0;
    std::size_t payload_bytes = 0;
    u32 width = 0;
    u32 height = 0;
    u32 frame_count = 0;
    u32 largest_frame_bytes = 0;
    u32 decoded_frames = 0;
    i32 requested_x = -1;
    i32 requested_y = -1;
    i32 target_x = 0;
    i32 target_y = 0;
    LONG volume = 0;
    LONG pan = 0;
    u32 surface_type = 0;
    u32 fade_steps = 0;
    u32 surface_clear_count = 0;
    bool centered = false;
    bool active = false;
    bool completed = false;
    bool failed = false;
    bool cancelled = false;
    bool bink_api_ready = false;
    bool played_with_bink = false;
    bool played_with_callback = false;
    bool frame_surface_configured = false;
};

using BinkVideoPlaybackCallback = bool (*)(
    const BinkVideoRuntimeState& state, const std::vector<u8>& payload, void* user_data);

HRESULT InitDirectDrawSubsystem(HWND window, int width, int height, int color_depth,
    bool windowed);
HRESULT ConfigureDirectDrawSurfaces(HWND window, int width, int height, int color_depth);
HRESULT ConfigureDirectDrawSurfaces(HWND window, int width, int height, int color_depth,
    bool windowed);
void ShutdownDirectDrawSubsystem(HWND window);
HRESULT PresentBackBufferToPrimary();
HRESULT LockBackBufferSpriteRenderTarget(SpriteRenderTarget& target);
HRESULT UnlockBackBufferSpriteRenderTarget();
void SetDirectDrawRegionCopyRect(const RECT& source_rect, LONG dest_x = 0, LONG dest_y = 0);
HRESULT CopyPrimaryRegionToBackBuffer();
HRESULT CopyBackBufferRegionToPrimary();
void HandlePrimarySurfaceLostRefresh();
void UpdateDirectDrawErrorString(HRESULT result);
void UpdateDirectDrawErrorString();
void PushPrimarySurfaceSnapshot();
void PushBackSurfaceSnapshot();
void PopPrimarySurfaceSnapshot();
void PopBackSurfaceSnapshot();
void ReleaseTopDirectDrawSurfaceSnapshot();
void ReleaseAllDirectDrawSurfaceSnapshots();
void BuildPixelBlendTables();
HRESULT HandleDirectDrawFrameBoundary();
void HandleBackBufferFadeToBlack(u32 steps = 0x20);
void HandleBackBufferFadeFromBlack(u32 steps = 0x20);
HRESULT FillPrimaryDirectDrawSurfaceBlack();
HRESULT FillBackDirectDrawSurfaceBlack();
void SendTrcRecordFatalErrorMessage(HWND window, const char* archive_name,
    u32 record_index);
void SendSetupWriteErrorMessage(HWND window, const char* path);
void SendGenericFatalErrorMessage(HWND window, const char* detail);
void SendWorkerModalPauseMessage(HWND window);
void SendWorkerModalResumeMessage(HWND window);
LRESULT SendExternalLaunchMessage(HWND window, const char* parameters);
void SendFrontendCallbackMessage(HWND window);
LRESULT SendFrontendGameModalMessage(HWND window, u32 action);
LRESULT SendP2PGameFlowModalMessage(HWND window);
LRESULT SendFrontendGameModalResumeMessage(HWND window, u32* modal_wait_flag = nullptr);
LRESULT SendReplayModalMessage(HWND window, u32 action);
LRESULT SendFrontendNetworkRouteMessage(HWND window, WPARAM wparam, LPARAM lparam);
void PaintMainWindowBlack(HWND window);
bool InitDirectSoundSubsystem(HWND window);
void ShutdownDirectSoundSubsystem();
bool BuildSecondarySoundBufferSlot(u32 slot_index, DWORD buffer_bytes, DWORD sample_rate,
    WORD bits_per_sample, WORD block_align, WORD channels, DWORD extra_flags);
HRESULT CreateDirectSoundBufferSlot(u32 slot_index, DWORD buffer_bytes, DWORD sample_rate,
    WORD bits_per_sample, WORD channels, DWORD extra_flags);
HRESULT CreateDirectSoundBufferSlot(u32 slot_index, DWORD buffer_bytes, DWORD sample_rate,
    WORD bits_per_sample, WORD block_align, WORD channels, DWORD extra_flags);
void ReleaseDirectSoundBufferSlot(u32 slot_index);
void ResetDirectSoundReservedBuffers();
void ReleaseReservedDirectSoundBuffers();
void ReleaseStoppedReservedSoundBuffers();
void ReleaseStoppedReservedDirectSoundBuffers();
bool HasFreeReservedSoundBuffer();
bool HasFreeReservedDirectSoundBuffer();
void SetCurrentDirectSoundBufferSlotIndex(u32 slot_index);
void SetCurrentDirectSoundBufferPlaybackState(u32 slot_index, LONG volume, LONG pan);
HRESULT PlayDirectSoundBufferSlot(u32 slot_index, DWORD play_flags);
void PlayCurrentSoundBufferSlot();
HRESULT DuplicateAndPlayReservedDirectSoundBuffer(u32 slot_index);
void DuplicateAndPlayReservedSoundBuffer();
HRESULT StopDirectSoundBufferSlot(u32 slot_index);
void StopCurrentSoundBufferSlot();
void ReleaseCurrentSoundBufferSlot();
HRESULT GetDirectSoundBufferSlotStatus(u32 slot_index, DWORD* status);
void GetCurrentSoundBufferStatus();
HRESULT GetDirectSoundBufferSlotFrequency(u32 slot_index, DWORD* frequency);
void GetCurrentSoundBufferFrequency();
HRESULT GetDirectSoundBufferSlotVolume(u32 slot_index, LONG* volume);
void GetCurrentSoundBufferVolume();
HRESULT GetDirectSoundBufferSlotPan(u32 slot_index, LONG* pan);
void GetCurrentSoundBufferPan();
HRESULT SetDirectSoundBufferSlotFrequency(u32 slot_index, DWORD frequency);
void SetCurrentSoundBufferFrequency();
HRESULT OffsetDirectSoundBufferSlotVolume(u32 slot_index, LONG volume_delta);
void AdjustCurrentSoundBufferVolume();
HRESULT OffsetDirectSoundBufferSlotPan(u32 slot_index, LONG pan_delta);
void AdjustCurrentSoundBufferPan();
u32 AllocateDirectSoundBufferSlotIndex();
void ReleaseDirectSoundBufferSlotsFrom(u32 first_slot);
void ReleaseAllDirectSoundBufferSlots();
void SetNextSoundBufferStaticFlag();
void SetNextDirectSoundBufferExtraFlags(DWORD extra_flags);
void ClearNextSoundBufferExtraFlags();
void ResetNextDirectSoundBufferExtraFlags();
bool UploadWaveFileToDirectSoundBuffer(LPDIRECTSOUNDBUFFER buffer, HANDLE file,
    DWORD byte_count);
bool UploadOpenTrcRecordToDirectSoundBuffer(LPDIRECTSOUNDBUFFER buffer,
    TrcRecordReader& reader, DWORD byte_count);
bool UploadMemoryWaveToDirectSoundBuffer(LPDIRECTSOUNDBUFFER buffer, const void* sample_data,
    DWORD byte_count);
u32 LoadWaveHandleIntoSoundBufferSlot(HANDLE file);
u32 LoadWaveHandleIntoDirectSoundBufferSlot(HANDLE file);
u32 LoadWaveFileIntoSoundBufferSlot(const char* path);
u32 LoadWaveFileIntoDirectSoundBufferSlot(const char* path);
u32 LoadOpenTrcWaveIntoSoundBufferSlot(TrcRecordReader& reader);
u32 LoadTrcWaveRecordIntoSoundBufferSlot(const char* archive_name, u32 record_index);
u32 LoadTrcWaveRecordIntoDirectSoundBufferSlot(const char* archive_name, u32 record_index);
u32 LoadMemoryWaveIntoSoundBufferSlot(const void* wave_data, u32* total_wave_bytes);
u32 LoadMemoryWaveIntoDirectSoundBufferSlot(const void* wave_data, u32* total_wave_bytes);
void SetBinkVideoPlaybackCallback(BinkVideoPlaybackCallback callback, void* user_data);
bool PlayBinkTrcRecord(const char* archive_name, u32 record_index, i32 x = -1, i32 y = -1);
bool PlayBinkSource(const void* source, u32 open_flags, i32 x = -1, i32 y = -1);
bool RenderBinkFrameToBackBuffer(void* bink_handle);
void CancelBinkVideoPlayback();
bool ConfigureBinkFrameSurface();
void HandleJw208IntroVideoSequence(HWND window);
void HandleJw208Record3VideoTransition(HWND window);

const DirectDrawRuntimeState& direct_draw_state();
const DirectSoundRuntimeState& direct_sound_state();
const BinkVideoRuntimeState& bink_video_state();
#endif

}
