#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace ranker {

#ifdef _WIN32
struct MixerControlState {
    HMIXER mixer = nullptr;
    DWORD control_id = 0;
    MMRESULT last_result = MMSYSERR_NOERROR;
    DWORD channels = 0;
    bool available = false;
};

struct CdAudioVolumeState {
    MixerControlState mixer_control;
    UINT aux_device_id = 0;
    DWORD original_volume = 0;
    DWORD cached_volume = 0;
    bool initialized = false;
    bool using_aux_device = false;
};

void InitializeMixerControlFields(MixerControlState& state);
bool InitializeMixerLineControl(MixerControlState& state, DWORD destination_component,
    DWORD source_component, DWORD control_type);
bool HandleMixerControlCallbackOpen(MixerControlState& state, DWORD_PTR callback_window,
    DWORD destination_component, DWORD source_component, DWORD control_type);
void ShutdownMixerControl(MixerControlState& state);
void DeleteMixerControl(MixerControlState* state, bool free_memory);
bool CheckMixerControlAvailable(const MixerControlState& state);
DWORD GetMixerControlValue(MixerControlState& state);
bool SetMixerControlValue(MixerControlState& state, DWORD value);
void HandleMixerControlClearValue(MixerControlState& state);
void HandleMixerControlUnitValue(MixerControlState& state);

CdAudioVolumeState& cd_audio_volume_state();
void InitializeCdAudioVolumeControl();
void ApplyCachedCdAudioVolume();
void HandleCdAudioVolumeRefresh();
#endif

}
