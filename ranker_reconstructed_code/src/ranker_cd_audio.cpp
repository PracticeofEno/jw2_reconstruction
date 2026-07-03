#include "ranker_cd_audio.h"

#ifdef _WIN32
#include "ranker_mixer.h"

#include <cstring>

namespace ranker {
namespace {

CdAudioMciState g_cd_audio_mci_state;

constexpr DWORD kOriginalCdAudioPositionStatusFlags = 0x4111;

void initialize_legacy_mci_scratch(DWORD_PTR& scratch) {
    std::memset(&scratch, 0xcc, sizeof(scratch));
}

void query_status_item(DWORD item, DWORD flags) {
    auto& state = g_cd_audio_mci_state;
    if (!state.open) {
        return;
    }

    MCI_STATUS_PARMS params;
    std::memset(&params, 0xcc, sizeof(params));
    params.dwItem = item;
    state.last_error = mciSendCommandA(state.device_id, MCI_STATUS, flags,
        reinterpret_cast<DWORD_PTR>(&params));
    state.last_status_value = static_cast<DWORD>(params.dwReturn);
}

} // namespace

CdAudioMciState& cd_audio_mci_state() {
    return g_cd_audio_mci_state;
}

bool InitializeCdAudioDevice(const char* element_name) {
    auto& state = g_cd_audio_mci_state;
    if (state.open) {
        return true;
    }

    MCI_OPEN_PARMSA open_params{};
    open_params.lpstrDeviceType = "cdaudio";
    open_params.lpstrElementName = element_name;
    state.device_id = 0;
    state.last_error = mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT,
        reinterpret_cast<DWORD_PTR>(&open_params));
    if (state.last_error != 0) {
        return false;
    }

    state.device_id = open_params.wDeviceID;
    MCI_SET_PARMS set_params{};
    set_params.dwTimeFormat = MCI_FORMAT_TMSF;
    state.last_error = mciSendCommandA(state.device_id, MCI_SET, MCI_SET_TIME_FORMAT,
        reinterpret_cast<DWORD_PTR>(&set_params));
    if (state.last_error != 0) {
        return false;
    }

    state.open = true;
    InitializeCdAudioVolumeControl();
    return true;
}

void HandleCdAudioDeviceShutdown() {
    auto& state = g_cd_audio_mci_state;
    if (!state.open) {
        return;
    }

    HandleCdAudioPlaybackStop();
    DWORD_PTR close_params;
    initialize_legacy_mci_scratch(close_params);
    state.last_error = mciSendCommandA(
        state.device_id, MCI_CLOSE, 0, reinterpret_cast<DWORD_PTR>(&close_params));
    auto& volume = cd_audio_volume_state();
    if (volume.initialized) {
        volume.cached_volume = volume.original_volume;
        ApplyCachedCdAudioVolume();
    }
    if (state.last_error == 0) {
        state.open = false;
    }
}

void HandleCdAudioPlaybackStart() {
    auto& state = g_cd_audio_mci_state;
    if (!state.open) {
        return;
    }

    MCI_PLAY_PARMS play{};
    play.dwFrom = state.current_track;
    play.dwTo = state.current_track + 1;
    state.last_error = mciSendCommandA(state.device_id, MCI_PLAY, MCI_FROM | MCI_TO,
        reinterpret_cast<DWORD_PTR>(&play));
}

void HandleCdAudioPlaybackStop() {
    auto& state = g_cd_audio_mci_state;
    if (!state.open) {
        return;
    }
    DWORD_PTR stop_params;
    initialize_legacy_mci_scratch(stop_params);
    state.last_error = mciSendCommandA(
        state.device_id, MCI_STOP, 0, reinterpret_cast<DWORD_PTR>(&stop_params));
}

void GetCdAudioPlayingMode() {
    query_status_item(MCI_STATUS_MODE, MCI_STATUS_ITEM);
    g_cd_audio_mci_state.playing =
        g_cd_audio_mci_state.last_status_value == static_cast<DWORD>(MCI_MODE_PLAY);
}

void GetCdAudioPositionStatus() {
    auto& state = g_cd_audio_mci_state;
    if (!state.open) {
        return;
    }

    MCI_STATUS_PARMS params;
    std::memset(&params, 0xcc, sizeof(params));
    params.dwTrack = MCI_STATUS_POSITION;
    state.last_error = mciSendCommandA(state.device_id, MCI_STATUS,
        kOriginalCdAudioPositionStatusFlags, reinterpret_cast<DWORD_PTR>(&params));
    state.last_status_value = static_cast<DWORD>(params.dwReturn);
}

void GetCdAudioMediaCount() {
    query_status_item(MCI_STATUS_NUMBER_OF_TRACKS, MCI_STATUS_ITEM);
}

void GetCdAudioLength() {
    query_status_item(MCI_STATUS_LENGTH, MCI_STATUS_ITEM);
}

}
#endif
