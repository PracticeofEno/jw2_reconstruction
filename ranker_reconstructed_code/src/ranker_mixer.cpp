#include "ranker_mixer.h"

#ifdef _WIN32

#include <cstdlib>
#include <cstring>

namespace ranker {
namespace {

CdAudioVolumeState g_cd_audio_volume_state;

constexpr DWORD kOriginalMixerSourceSkipSearch = 0x100b;

void initialize_mixer_control_details(MixerControlState& state,
    MIXERCONTROLDETAILS& details, MIXERCONTROLDETAILS_UNSIGNED& value) {
    std::memset(&value, 0xcc, sizeof(value));
    std::memset(&details, 0xcc, sizeof(details));
    details.cbStruct = sizeof(details);
    details.dwControlID = state.control_id;
    details.cChannels = state.channels;
    details.cMultipleItems = 0;
    details.cbDetails = sizeof(value);
    details.paDetails = &value;
}

bool find_cd_aux_device(UINT& device_id) {
    const UINT count = auxGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        AUXCAPSA caps;
        std::memset(&caps, 0xcc, sizeof(caps));
        auxGetDevCapsA(i, &caps, sizeof(caps));
        if (caps.wTechnology == AUXCAPS_CDAUDIO) {
            device_id = i;
            return true;
        }
    }
    return false;
}

bool open_mixer_line_control(MixerControlState& state, DWORD_PTR callback_window,
    DWORD callback_flags, DWORD destination_component, DWORD source_component,
    DWORD control_type) {
    InitializeMixerControlFields(state);
    if (mixerGetNumDevs() == 0) {
        return false;
    }

    state.last_result = mixerOpen(&state.mixer, 0, callback_window, 0, callback_flags);
    if (state.last_result != MMSYSERR_NOERROR) {
        return false;
    }

    MIXERLINEA line{};
    line.cbStruct = sizeof(line);
    line.dwComponentType = destination_component;
    MMRESULT result = mixerGetLineInfoA(
        reinterpret_cast<HMIXEROBJ>(state.mixer), &line, MIXER_GETLINEINFOF_COMPONENTTYPE);
    if (result != MMSYSERR_NOERROR) {
        return false;
    }

    if (source_component != kOriginalMixerSourceSkipSearch) {
        const DWORD destination = line.dwDestination;
        const DWORD connections = line.cConnections;
        for (DWORD i = 0; i < connections; ++i) {
            line.cbStruct = sizeof(line);
            line.dwDestination = destination;
            line.dwSource = i;
            result = mixerGetLineInfoA(
                reinterpret_cast<HMIXEROBJ>(state.mixer), &line, MIXER_GETLINEINFOF_SOURCE);
            if (result != MMSYSERR_NOERROR) {
                return false;
            }
            if (line.dwComponentType == source_component) {
                break;
            }
        }
    }

    MIXERCONTROLA control{};
    MIXERLINECONTROLSA controls{};
    controls.cbStruct = sizeof(controls);
    controls.dwLineID = line.dwLineID;
    controls.dwControlType = control_type;
    controls.cControls = 1;
    controls.cbmxctrl = sizeof(control);
    controls.pamxctrl = &control;
    result = mixerGetLineControlsA(reinterpret_cast<HMIXEROBJ>(state.mixer),
        &controls, MIXER_GETLINECONTROLSF_ONEBYTYPE);
    if (result != MMSYSERR_NOERROR) {
        return false;
    }

    state.control_id = control.dwControlID;
    state.channels = line.cChannels != 0 ? line.cChannels - 1 : 0;
    state.available = true;
    return true;
}

} // namespace

void InitializeMixerControlFields(MixerControlState& state) {
    state.mixer = nullptr;
    state.control_id = 0;
    state.last_result = MMSYSERR_NOERROR;
    state.channels = 0;
    state.available = false;
}

bool InitializeMixerLineControl(MixerControlState& state, DWORD destination_component,
    DWORD source_component, DWORD control_type) {
    return open_mixer_line_control(state, 0, 0, destination_component, source_component,
        control_type);
}

bool HandleMixerControlCallbackOpen(MixerControlState& state, DWORD_PTR callback_window,
    DWORD destination_component, DWORD source_component, DWORD control_type) {
    return open_mixer_line_control(state, callback_window, CALLBACK_WINDOW,
        destination_component, source_component, control_type);
}

void ShutdownMixerControl(MixerControlState& state) {
    if (state.mixer != nullptr) {
        mixerClose(state.mixer);
    }
}

void DeleteMixerControl(MixerControlState* state, bool free_memory) {
    if (state == nullptr) {
        return;
    }
    ShutdownMixerControl(*state);
    if (free_memory) {
        std::free(state);
    }
}

bool CheckMixerControlAvailable(const MixerControlState& state) {
    return state.available;
}

DWORD GetMixerControlValue(MixerControlState& state) {
    if (!state.available) {
        return 0;
    }

    state.available = false;
    MIXERCONTROLDETAILS_UNSIGNED value;
    MIXERCONTROLDETAILS details;
    initialize_mixer_control_details(state, details, value);
    state.last_result = mixerGetControlDetailsA(
        reinterpret_cast<HMIXEROBJ>(state.mixer), &details, MIXER_GETCONTROLDETAILSF_VALUE);
    if (state.last_result == MMSYSERR_NOERROR) {
        state.available = true;
        return value.dwValue;
    }
    return 0;
}

bool SetMixerControlValue(MixerControlState& state, DWORD value) {
    if (!state.available) {
        return false;
    }

    state.available = false;
    MIXERCONTROLDETAILS_UNSIGNED detail;
    MIXERCONTROLDETAILS details;
    initialize_mixer_control_details(state, details, detail);

    state.last_result = mixerGetControlDetailsA(
        reinterpret_cast<HMIXEROBJ>(state.mixer), &details, MIXER_GETCONTROLDETAILSF_VALUE);
    if (state.last_result == MMSYSERR_NOERROR) {
        detail.dwValue = value;
        state.last_result = mixerSetControlDetails(
            reinterpret_cast<HMIXEROBJ>(state.mixer), &details, MIXER_SETCONTROLDETAILSF_VALUE);
        if (state.last_result == MMSYSERR_NOERROR) {
            state.available = true;
        }
    }
    return state.available;
}

void HandleMixerControlClearValue(MixerControlState& state) {
    SetMixerControlValue(state, 0);
}

void HandleMixerControlUnitValue(MixerControlState& state) {
    SetMixerControlValue(state, 1);
}

CdAudioVolumeState& cd_audio_volume_state() {
    return g_cd_audio_volume_state;
}

void InitializeCdAudioVolumeControl() {
    auto& state = g_cd_audio_volume_state;
    if (state.initialized) {
        return;
    }

    state.using_aux_device = false;
    InitializeMixerLineControl(state.mixer_control, MIXERLINE_COMPONENTTYPE_DST_SPEAKERS,
        MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC, MIXERCONTROL_CONTROLTYPE_VOLUME);
    if (!CheckMixerControlAvailable(state.mixer_control)) {
        if (find_cd_aux_device(state.aux_device_id)) {
            state.using_aux_device = true;
            state.initialized = true;
            HandleCdAudioVolumeRefresh();
            auxGetVolume(state.aux_device_id, &state.original_volume);
        }
        ShutdownMixerControl(state.mixer_control);
        return;
    }

    state.original_volume = GetMixerControlValue(state.mixer_control);
    state.initialized = true;
    ShutdownMixerControl(state.mixer_control);
}

void ApplyCachedCdAudioVolume() {
    auto& state = g_cd_audio_volume_state;
    if (!state.initialized) {
        return;
    }

    InitializeMixerLineControl(state.mixer_control, MIXERLINE_COMPONENTTYPE_DST_SPEAKERS,
        MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC, MIXERCONTROL_CONTROLTYPE_VOLUME);
    if (!CheckMixerControlAvailable(state.mixer_control)) {
        const DWORD low = state.cached_volume & 0xffffu;
        state.cached_volume = (state.cached_volume << 16) | low;
        auxSetVolume(state.aux_device_id, state.cached_volume);
        ShutdownMixerControl(state.mixer_control);
        return;
    }

    SetMixerControlValue(state.mixer_control, state.cached_volume);
    ShutdownMixerControl(state.mixer_control);
}

void HandleCdAudioVolumeRefresh() {
    auto& state = g_cd_audio_volume_state;
    if (!state.initialized) {
        return;
    }

    InitializeMixerLineControl(state.mixer_control, MIXERLINE_COMPONENTTYPE_DST_SPEAKERS,
        MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC, MIXERCONTROL_CONTROLTYPE_VOLUME);
    if (!CheckMixerControlAvailable(state.mixer_control)) {
        auxGetVolume(state.aux_device_id, &state.cached_volume);
        state.cached_volume &= 0xffffu;
        ShutdownMixerControl(state.mixer_control);
        return;
    }

    state.cached_volume = GetMixerControlValue(state.mixer_control);
    ShutdownMixerControl(state.mixer_control);
}

}
#endif
