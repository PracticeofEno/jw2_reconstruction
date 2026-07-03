#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace ranker {

#ifdef _WIN32
struct CdAudioMciState {
    MCIDEVICEID device_id = 0;
    MCIERROR last_error = 0;
    DWORD current_track = 0;
    DWORD last_status_value = 0;
    bool open = false;
    bool playing = false;
};

CdAudioMciState& cd_audio_mci_state();
bool InitializeCdAudioDevice(const char* element_name);
void HandleCdAudioDeviceShutdown();
void HandleCdAudioPlaybackStart();
void HandleCdAudioPlaybackStop();
void GetCdAudioPlayingMode();
void GetCdAudioPositionStatus();
void GetCdAudioMediaCount();
void GetCdAudioLength();
#endif

}
