#include "ranker_directx.h"
#include "ranker_cursor.h"
#include "ranker_miles.h"
#include "ranker_palette_cache.h"
#include "ranker_sprite_renderer.h"
#include "ranker_trc.h"

#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ranker {
namespace {

DirectDrawRuntimeState g_direct_draw_state;
DirectSoundRuntimeState g_direct_sound_state;
BinkVideoRuntimeState g_bink_video_state;
BinkVideoPlaybackCallback g_bink_video_callback = nullptr;
void* g_bink_video_callback_user_data = nullptr;

constexpr u32 kMaxDirectSoundBufferSlots = 0x32a;
constexpr u32 kInvalidDirectSoundBufferSlot = 0xffffffffu;
constexpr u32 kDirectDrawSurfaceSnapshotCount = 8;
constexpr u32 kOriginalDirectDrawSurfaceSnapshotMaxDepth = 7;
constexpr DWORD kOriginalDirectSoundLockFlags = 1;
constexpr u32 kBinkOpenFromFileHandle = 0x00800000u;
constexpr u32 kBinkOpenFromMemory = 0x04000000u;
constexpr u32 kBinkCopyDirectDrawFlags = 0x00080000u;

struct PcmWaveInfo {
    DWORD riff_total_bytes = 0;
    DWORD data_bytes = 0;
    DWORD sample_rate = 0;
    WORD bits_per_sample = 0;
    WORD block_align = 0;
    WORD channels = 0;
    const u8* sample_data = nullptr;
};

struct BinkMovieHeaderPrefix {
    u32 width = 0;
    u32 height = 0;
    u32 frame_count = 0;
    u32 frame_number = 0;
};

struct BinkFileHeaderInfo {
    u32 width = 0;
    u32 height = 0;
    u32 frame_count = 0;
    u32 largest_frame_bytes = 0;
};

void ensure_direct_sound_slots();

struct BinkApi {
    using OpenDirectSoundFn = void* (WINAPI*)(void*);
    using SetSoundSystemFn = i32 (WINAPI*)(OpenDirectSoundFn, void*);
    using DdSurfaceTypeFn = u32 (WINAPI*)(void*);
    using OpenFn = BinkMovieHeaderPrefix* (WINAPI*)(const void*, u32);
    using CloseFn = void (WINAPI*)(BinkMovieHeaderPrefix*);
    using WaitFn = i32 (WINAPI*)(BinkMovieHeaderPrefix*);
    using DoFrameFn = void (WINAPI*)(BinkMovieHeaderPrefix*);
    using NextFrameFn = void (WINAPI*)(BinkMovieHeaderPrefix*);
    using SetVolumeFn = void (WINAPI*)(BinkMovieHeaderPrefix*, u32, i32);
    using SetPanFn = void (WINAPI*)(BinkMovieHeaderPrefix*, u32, i32);
    using CopyToBufferFn = void (WINAPI*)(BinkMovieHeaderPrefix*, void*, i32, u32, i32, i32, u32);

    HMODULE module = nullptr;
    OpenDirectSoundFn open_direct_sound = nullptr;
    SetSoundSystemFn set_sound_system = nullptr;
    DdSurfaceTypeFn dd_surface_type = nullptr;
    OpenFn open = nullptr;
    CloseFn close = nullptr;
    WaitFn wait = nullptr;
    DoFrameFn do_frame = nullptr;
    NextFrameFn next_frame = nullptr;
    SetVolumeFn set_volume = nullptr;
    SetPanFn set_pan = nullptr;
    CopyToBufferFn copy_to_buffer = nullptr;

    bool ready() const {
        return module != nullptr && open_direct_sound != nullptr && set_sound_system != nullptr &&
            dd_surface_type != nullptr && open != nullptr && close != nullptr &&
            wait != nullptr && do_frame != nullptr && next_frame != nullptr &&
            set_volume != nullptr && set_pan != nullptr && copy_to_buffer != nullptr;
    }
};

template <typename T>
void release_com(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

HRESULT direct_sound_slot_error() {
    return DSERR_INVALIDCALL;
}

const char* direct_draw_error_name(HRESULT result) {
    switch (result) {
    case DD_OK:
        return "DD_OK";
    case DDERR_ALREADYINITIALIZED:
        return "DDERR_ALREADYINITIALIZED";
    case DDERR_BLTFASTCANTCLIP:
        return "DDERR_BLTFASTCANTCLIP";
    case DDERR_CANNOTATTACHSURFACE:
        return "DDERR_CANNOTATTACHSURFACE";
    case DDERR_CANNOTDETACHSURFACE:
        return "DDERR_CANNOTDETACHSURFACE";
    case DDERR_CANTCREATEDC:
        return "DDERR_CANTCREATEDC";
    case DDERR_CANTLOCKSURFACE:
        return "DDERR_CANTLOCKSURFACE";
    case DDERR_CLIPPERISUSINGHWND:
        return "DDERR_CLIPPERISUSINGHWND";
    case DDERR_COLORKEYNOTSET:
        return "DDERR_COLORKEYNOTSET";
    case DDERR_CURRENTLYNOTAVAIL:
        return "DDERR_CURRENTLYNOTAVAIL";
    case DDERR_EXCEPTION:
        return "DDERR_EXCEPTION";
    case DDERR_EXCLUSIVEMODEALREADYSET:
        return "DDERR_EXCLUSIVEMODEALREADYSET";
    case DDERR_GENERIC:
        return "DDERR_GENERIC";
    case DDERR_HWNDALREADYSET:
        return "DDERR_HWNDALREADYSET";
    case DDERR_INVALIDCAPS:
        return "DDERR_INVALIDCAPS";
    case DDERR_INVALIDCLIPLIST:
        return "DDERR_INVALIDCLIPLIST";
    case DDERR_INVALIDDIRECTDRAWGUID:
        return "DDERR_INVALIDDIRECTDRAWGUID";
    case DDERR_INVALIDMODE:
        return "DDERR_INVALIDMODE";
    case DDERR_INVALIDOBJECT:
        return "DDERR_INVALIDOBJECT";
    case DDERR_INVALIDPARAMS:
        return "DDERR_INVALIDPARAMS";
    case DDERR_INVALIDPIXELFORMAT:
        return "DDERR_INVALIDPIXELFORMAT";
    case DDERR_INVALIDRECT:
        return "DDERR_INVALIDRECT";
    case DDERR_NOCLIPPERATTACHED:
        return "DDERR_NOCLIPPERATTACHED";
    case DDERR_NOCOOPERATIVELEVELSET:
        return "DDERR_NOCOOPERATIVELEVELSET";
    case DDERR_NODIRECTDRAWHW:
        return "DDERR_NODIRECTDRAWHW";
    case DDERR_NODIRECTDRAWSUPPORT:
        return "DDERR_NODIRECTDRAWSUPPORT";
    case DDERR_NOEXCLUSIVEMODE:
        return "DDERR_NOEXCLUSIVEMODE";
    case DDERR_OUTOFMEMORY:
        return "DDERR_OUTOFMEMORY";
    case DDERR_OUTOFVIDEOMEMORY:
        return "DDERR_OUTOFVIDEOMEMORY";
    case DDERR_PRIMARYSURFACEALREADYEXISTS:
        return "DDERR_PRIMARYSURFACEALREADYEXISTS";
    case DDERR_SURFACEBUSY:
        return "DDERR_SURFACEBUSY";
    case DDERR_SURFACELOST:
        return "DDERR_SURFACELOST";
    case DDERR_UNSUPPORTED:
        return "DDERR_UNSUPPORTED";
    case DDERR_UNSUPPORTEDFORMAT:
        return "DDERR_UNSUPPORTEDFORMAT";
    case DDERR_UNSUPPORTEDMODE:
        return "DDERR_UNSUPPORTEDMODE";
    case DDERR_WASSTILLDRAWING:
        return "DDERR_WASSTILLDRAWING";
    case DDERR_WRONGMODE:
        return "DDERR_WRONGMODE";
    default:
        return "DDERR_UNKNOW ";
    }
}

LPDIRECTSOUNDBUFFER direct_sound_buffer_slot(u32 slot_index) {
    if (slot_index >= g_direct_sound_state.secondary_buffers.size()) {
        return nullptr;
    }
    return g_direct_sound_state.secondary_buffers[slot_index];
}

u16 read_le_u16(const u8* p) {
    return static_cast<u16>(p[0]) | static_cast<u16>(p[1] << 8);
}

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

bool tag_equals(const u8* p, const char* tag) {
    return std::memcmp(p, tag, 4) == 0;
}

template <typename T>
T load_bink_proc(HMODULE module, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (FARPROC proc = GetProcAddress(module, name)) {
            return reinterpret_cast<T>(proc);
        }
    }
    return nullptr;
}

BinkApi& bink_api() {
    static BinkApi api;
    static bool attempted = false;
    if (attempted) {
        return api;
    }

    attempted = true;
    api.module = LoadLibraryA("binkw32.dll");
    if (api.module == nullptr) {
        return api;
    }

    api.open_direct_sound = load_bink_proc<BinkApi::OpenDirectSoundFn>(
        api.module, { "_BinkOpenDirectSound@4", "BinkOpenDirectSound" });
    api.set_sound_system = load_bink_proc<BinkApi::SetSoundSystemFn>(
        api.module, { "_BinkSetSoundSystem@8", "BinkSetSoundSystem" });
    api.dd_surface_type = load_bink_proc<BinkApi::DdSurfaceTypeFn>(
        api.module, { "_BinkDDSurfaceType@4", "BinkDDSurfaceType" });
    api.open = load_bink_proc<BinkApi::OpenFn>(api.module, { "_BinkOpen@8", "BinkOpen" });
    api.close = load_bink_proc<BinkApi::CloseFn>(api.module, { "_BinkClose@4", "BinkClose" });
    api.wait = load_bink_proc<BinkApi::WaitFn>(api.module, { "_BinkWait@4", "BinkWait" });
    api.do_frame =
        load_bink_proc<BinkApi::DoFrameFn>(api.module, { "_BinkDoFrame@4", "BinkDoFrame" });
    api.next_frame = load_bink_proc<BinkApi::NextFrameFn>(
        api.module, { "_BinkNextFrame@4", "BinkNextFrame" });
    api.set_volume = load_bink_proc<BinkApi::SetVolumeFn>(
        api.module, { "_BinkSetVolume@12", "BinkSetVolume" });
    api.set_pan = load_bink_proc<BinkApi::SetPanFn>(
        api.module, { "_BinkSetPan@12", "BinkSetPan" });
    api.copy_to_buffer = load_bink_proc<BinkApi::CopyToBufferFn>(
        api.module, { "_BinkCopyToBuffer@28", "BinkCopyToBuffer" });

    if (!api.ready()) {
        FreeLibrary(api.module);
        api = BinkApi{};
    }
    return api;
}

bool parse_bink_file_header(const std::vector<u8>& payload, BinkFileHeaderInfo& info) {
    if (payload.size() < 0x20 || payload[0] != 'B' || payload[1] != 'I' ||
        payload[2] != 'K') {
        return false;
    }

    info.frame_count = read_le_u32(payload.data() + 0x08);
    info.largest_frame_bytes = read_le_u32(payload.data() + 0x0c);
    info.width = read_le_u32(payload.data() + 0x14);
    info.height = read_le_u32(payload.data() + 0x18);
    return info.width != 0 && info.height != 0 && info.width < 10000 &&
        info.height < 10000;
}

void prepare_bink_video_state(const char* archive_name, u32 record_index, i32 x, i32 y) {
    const u32 preserved_fade_steps = g_bink_video_state.fade_steps;
    const u32 preserved_clear_count = g_bink_video_state.surface_clear_count;
    g_bink_video_state = BinkVideoRuntimeState{};
    g_bink_video_state.archive_name = archive_name != nullptr ? archive_name : "";
    g_bink_video_state.record_index = record_index;
    g_bink_video_state.requested_x = x;
    g_bink_video_state.requested_y = y;
    g_bink_video_state.fade_steps = preserved_fade_steps;
    g_bink_video_state.surface_clear_count = preserved_clear_count;
}

void apply_bink_header_to_state(const BinkFileHeaderInfo& info) {
    g_bink_video_state.width = info.width;
    g_bink_video_state.height = info.height;
    g_bink_video_state.frame_count = info.frame_count;
    g_bink_video_state.largest_frame_bytes = info.largest_frame_bytes;
}

i32 centered_coordinate(u32 outer, u32 inner) {
    if (inner >= outer) {
        return 0;
    }
    return static_cast<i32>((outer - inner) >> 1);
}

void update_bink_target_coordinates(i32 x, i32 y) {
    if (y == -1) {
        g_bink_video_state.centered = true;
        g_bink_video_state.target_x =
            centered_coordinate(g_direct_draw_state.width, g_bink_video_state.width);
        g_bink_video_state.target_y =
            centered_coordinate(g_direct_draw_state.height, g_bink_video_state.height);
        return;
    }

    g_bink_video_state.centered = false;
    g_bink_video_state.target_x = x;
    g_bink_video_state.target_y = y;
}

u32 lowest_component_bit(u32 mask) {
    return mask & (~mask + 1u);
}

u16 darken_16bit_pixel(u16 pixel, u32 red_mask, u32 green_mask, u32 blue_mask,
    u32 red_step, u32 green_step, u32 blue_step) {
    auto darken_component = [](u32 pixel_value, u32 mask, u32 step) {
        if (mask == 0 || step == 0) {
            return 0u;
        }
        u32 component = pixel_value & mask;
        if (component >= step) {
            component -= step;
        }
        else {
            component = 0;
        }
        return component & mask;
    };

    const u32 value = pixel;
    return static_cast<u16>(
        darken_component(value, red_mask, red_step) |
        darken_component(value, green_mask, green_step) |
        darken_component(value, blue_mask, blue_step));
}

u16 limit_16bit_pixel(u16 original, u32 red_mask, u32 green_mask, u32 blue_mask,
    u32 red_limit, u32 green_limit, u32 blue_limit) {
    const u32 value = original;
    const u32 red = std::min(value & red_mask, red_limit & red_mask);
    const u32 green = std::min(value & green_mask, green_limit & green_mask);
    const u32 blue = std::min(value & blue_mask, blue_limit & blue_mask);
    return static_cast<u16>(red | green | blue);
}

HRESULT fill_direct_draw_surface_black(LPDIRECTDRAWSURFACE7 surface) {
    if (surface == nullptr) {
        return DDERR_GENERIC;
    }

    DDBLTFX fx{};
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = 0;

    HRESULT result = DD_OK;
    for (int attempt = 0; attempt < 2; ++attempt) {
        result = surface->Blt(nullptr, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
        if (result != DDERR_SURFACELOST) {
            break;
        }
        const HRESULT restore_result = surface->Restore();
        if (FAILED(restore_result)) {
            return restore_result;
        }
    }
    return result;
}

HRESULT darken_back_surface_once() {
    SpriteRenderTarget target{};
    HRESULT result = LockBackBufferSpriteRenderTarget(target);
    if (FAILED(result)) {
        return result;
    }

    u32 red_mask = g_direct_draw_state.red_mask != 0 ? g_direct_draw_state.red_mask : 0x7c00;
    u32 green_mask =
        g_direct_draw_state.green_mask != 0 ? g_direct_draw_state.green_mask : 0x03e0;
    u32 blue_mask =
        g_direct_draw_state.blue_mask != 0 ? g_direct_draw_state.blue_mask : 0x001f;
    u32 red_step = lowest_component_bit(red_mask);
    u32 green_step = lowest_component_bit(green_mask);
    u32 blue_step = lowest_component_bit(blue_mask);
    if (green_mask == 0x07e0) {
        green_mask = 0x07c0;
        green_step = 0x0040;
    }

    for (u32 y = 0; y < target.height; ++y) {
        auto* row = target.pixels + static_cast<std::size_t>(y) * target.stride_words;
        for (u32 x = 0; x < target.width; ++x) {
            row[x] = darken_16bit_pixel(row[x], red_mask, green_mask, blue_mask,
                red_step, green_step, blue_step);
        }
    }

    result = UnlockBackBufferSpriteRenderTarget();
    return result;
}

bool copy_bink_frame_to_back_buffer(BinkApi& api, BinkMovieHeaderPrefix* handle) {
    if (handle == nullptr) {
        return false;
    }

    SpriteRenderTarget target{};
    if (FAILED(LockBackBufferSpriteRenderTarget(target))) {
        return false;
    }

    api.copy_to_buffer(handle, target.pixels,
        static_cast<i32>(target.stride_words * sizeof(u16)), handle->height,
        g_bink_video_state.target_x, g_bink_video_state.target_y,
        g_bink_video_state.surface_type | kBinkCopyDirectDrawFlags);
    UnlockBackBufferSpriteRenderTarget();
    PresentBackBufferToPrimary();
    return true;
}

bool render_bink_frame_with_api(BinkApi& api, BinkMovieHeaderPrefix* handle) {
    if (!api.ready() || handle == nullptr) {
        return false;
    }

    api.do_frame(handle);
    if (!copy_bink_frame_to_back_buffer(api, handle)) {
        return false;
    }

    ++g_bink_video_state.decoded_frames;
    if (handle->frame_count != 1) {
        if (handle->frame_number == handle->frame_count) {
            g_bink_video_state.cancelled = true;
        }
        else {
            api.next_frame(handle);
        }
    }
    else {
        g_bink_video_state.cancelled = true;
    }
    return true;
}

bool play_open_bink_handle_with_api(BinkApi& api, BinkMovieHeaderPrefix* handle, i32 x, i32 y) {
    if (!api.ready() || handle == nullptr) {
        return false;
    }

    g_bink_video_state.width = handle->width;
    g_bink_video_state.height = handle->height;
    g_bink_video_state.frame_count = handle->frame_count;
    g_bink_video_state.volume = g_direct_sound_state.last_volume;
    g_bink_video_state.pan = g_direct_sound_state.last_pan;
    update_bink_target_coordinates(x, y);

    api.set_volume(handle, 0, g_bink_video_state.volume);
    api.set_pan(handle, 0, g_bink_video_state.pan);

    g_bink_video_state.active = true;
    bool ok = true;
    const u32 max_frames = std::max<u32>(1, handle->frame_count + 1);
    while (!g_bink_video_state.cancelled && g_bink_video_state.decoded_frames < max_frames &&
        handle->frame_number <= handle->frame_count) {
        ReleaseStoppedReservedDirectSoundBuffers();
        if (api.wait(handle) == 0 && !render_bink_frame_with_api(api, handle)) {
            ok = false;
            break;
        }
    }

    g_bink_video_state.active = false;
    g_bink_video_state.played_with_bink = ok;
    return ok;
}

bool play_bink_payload_with_api(const std::vector<u8>& payload, const char* archive_name,
    u32 payload_offset, bool prefer_file_handle) {
    BinkApi& api = bink_api();
    g_bink_video_state.bink_api_ready = api.ready();
    if (!api.ready() || !g_direct_draw_state.active || g_direct_draw_state.back_surface == nullptr) {
        return false;
    }

    api.set_sound_system(api.open_direct_sound,
        reinterpret_cast<void*>(g_direct_sound_state.direct_sound));
    g_bink_video_state.surface_type = api.dd_surface_type(g_direct_draw_state.back_surface);

    HANDLE bink_file = INVALID_HANDLE_VALUE;
    BinkMovieHeaderPrefix* handle = nullptr;
    if (prefer_file_handle && archive_name != nullptr) {
        bink_file = CreateFileA(archive_name, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (bink_file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER position{};
            position.QuadPart = payload_offset;
            if (SetFilePointerEx(bink_file, position, nullptr, FILE_BEGIN) != FALSE) {
                handle = api.open(reinterpret_cast<const void*>(bink_file),
                    kBinkOpenFromFileHandle);
            }
            if (handle == nullptr) {
                CloseHandle(bink_file);
                bink_file = INVALID_HANDLE_VALUE;
            }
        }
    }

    if (handle == nullptr) {
        handle = api.open(payload.data(), kBinkOpenFromMemory);
    }
    if (handle == nullptr) {
        if (bink_file != INVALID_HANDLE_VALUE) {
            CloseHandle(bink_file);
        }
        return false;
    }

    const bool ok = play_open_bink_handle_with_api(api, handle,
        g_bink_video_state.requested_x, g_bink_video_state.requested_y);
    api.close(handle);
    if (bink_file != INVALID_HANDLE_VALUE) {
        CloseHandle(bink_file);
    }
    return ok;
}

bool read_exact_file(HANDLE file, void* out, DWORD byte_count) {
    if (byte_count == 0) {
        return true;
    }
    if (file == INVALID_HANDLE_VALUE || out == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return false;
    }

    DWORD bytes_read = 0;
    if (!ReadFile(file, out, byte_count, &bytes_read, nullptr) || bytes_read != byte_count) {
        return false;
    }
    return true;
}

bool seek_file_current(HANDLE file, LONG byte_count) {
    LARGE_INTEGER distance{};
    distance.QuadPart = byte_count;
    if (SetFilePointerEx(file, distance, nullptr, FILE_CURRENT) != FALSE) {
        return true;
    }

    return false;
}

bool seek_file_absolute(HANDLE file, DWORD byte_count) {
    LARGE_INTEGER position{};
    position.QuadPart = byte_count;
    if (SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE) {
        return true;
    }

    return false;
}

bool current_file_position(HANDLE file, DWORD& position) {
    LARGE_INTEGER distance{};
    LARGE_INTEGER current{};
    if (SetFilePointerEx(file, distance, &current, FILE_CURRENT) == FALSE ||
        current.QuadPart < 0 || current.QuadPart > 0xffffffffll) {
        return false;
    }

    position = static_cast<DWORD>(current.QuadPart);
    return true;
}

bool parse_pcm_wave_header(const u8* header, PcmWaveInfo& info) {
    if (!tag_equals(header + 8, "WAVE") || !tag_equals(header + 12, "fmt ")) {
        return false;
    }

    const DWORD fmt_size = read_le_u32(header + 16);
    const WORD format_tag = read_le_u16(header + 20);
    if (format_tag != WAVE_FORMAT_PCM || fmt_size < 0x10) {
        return false;
    }

    info.riff_total_bytes = read_le_u32(header + 4) + 8;
    info.channels = read_le_u16(header + 22);
    info.sample_rate = read_le_u32(header + 24);
    info.block_align = read_le_u16(header + 32);
    info.bits_per_sample = read_le_u16(header + 34);
    return true;
}

bool read_pcm_wave_header_from_file(HANDLE file, PcmWaveInfo& info) {
    std::array<u8, 0x24> header{};
    if (!read_exact_file(file, header.data(), static_cast<DWORD>(header.size())) ||
        !parse_pcm_wave_header(header.data(), info)) {
        return false;
    }

    const DWORD fmt_size = read_le_u32(header.data() + 16);
    if (fmt_size > 0x10 && !seek_file_current(file, static_cast<LONG>(fmt_size - 0x10))) {
        return false;
    }

    std::array<u8, 8> data_header{};
    if (!read_exact_file(file, data_header.data(), static_cast<DWORD>(data_header.size())) ||
        !tag_equals(data_header.data(), "data")) {
        return false;
    }

    info.data_bytes = read_le_u32(data_header.data() + 4);
    return true;
}

bool parse_pcm_wave_memory(const void* wave_data, PcmWaveInfo& info) {
    if (wave_data == nullptr) {
        return false;
    }

    const auto* bytes = static_cast<const u8*>(wave_data);
    if (!parse_pcm_wave_header(bytes, info)) {
        return false;
    }

    const DWORD fmt_size = read_le_u32(bytes + 16);
    const u8* data_header = bytes + 0x14 + fmt_size;
    if (!tag_equals(data_header, "data")) {
        return false;
    }

    info.data_bytes = read_le_u32(data_header + 4);
    info.sample_data = data_header + 8;
    return true;
}

bool parse_trc_pcm_wave_stream_header(const u8* bytes, PcmWaveInfo& info) {
    if (!tag_equals(bytes + 8, "WAVE") || !tag_equals(bytes + 12, "fmt ") ||
        read_le_u16(bytes + 20) != WAVE_FORMAT_PCM) {
        return false;
    }

    info.riff_total_bytes = read_le_u32(bytes + 4) + 8;
    info.channels = read_le_u16(bytes + 22);
    info.sample_rate = read_le_u32(bytes + 24);
    info.block_align = read_le_u16(bytes + 32);
    info.bits_per_sample = read_le_u16(bytes + 34);
    return true;
}

bool discard_open_trc_record_bytes(TrcRecordReader& reader,
    std::size_t byte_count) {
    std::array<u8, 4096> scratch{};
    while (byte_count != 0) {
        const std::size_t chunk = std::min<std::size_t>(
            byte_count, scratch.size());
        if (!ReadOpenTrcRecordBytes(reader, scratch.data(), chunk)) {
            return false;
        }
        byte_count -= chunk;
    }
    return true;
}

bool finish_trc_pcm_wave_stream(TrcRecordReader& reader,
    std::size_t start_cursor, const PcmWaveInfo& info) {
    if (info.riff_total_bytes >
            std::numeric_limits<std::size_t>::max() - start_cursor) {
        return false;
    }
    const std::size_t end_cursor =
        start_cursor + static_cast<std::size_t>(info.riff_total_bytes);
    if (reader.cursor > end_cursor) {
        return false;
    }
    return discard_open_trc_record_bytes(reader, end_cursor - reader.cursor);
}

bool read_trc_pcm_wave_stream_header(TrcRecordReader& reader, PcmWaveInfo& info) {
    std::array<u8, 0x24> header{};
    if (!ReadOpenTrcRecordBytes(reader, header.data(), header.size()) ||
        !parse_trc_pcm_wave_stream_header(header.data(), info)) {
        return false;
    }

    const DWORD fmt_size = read_le_u32(header.data() + 16);
    if (fmt_size < 0x10 ||
        (fmt_size > 0x10 && !discard_open_trc_record_bytes(
            reader, static_cast<std::size_t>(fmt_size - 0x10)))) {
        return false;
    }

    std::array<u8, 8> data_header{};
    if (!ReadOpenTrcRecordBytes(reader, data_header.data(), data_header.size()) ||
        !tag_equals(data_header.data(), "data")) {
        return false;
    }

    info.data_bytes = read_le_u32(data_header.data() + 4);
    return true;
}

template <typename Reader>
bool upload_direct_sound_buffer(LPDIRECTSOUNDBUFFER buffer, DWORD byte_count, Reader&& reader) {
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return false;
    }

    LPVOID first = nullptr;
    DWORD first_bytes = 0;
    LPVOID second = nullptr;
    DWORD second_bytes = 0;
    HRESULT result = buffer->Lock(0, byte_count, &first, &first_bytes, &second, &second_bytes,
        kOriginalDirectSoundLockFlags);
    if (FAILED(result)) {
        return false;
    }

    bool ok = true;
    if (first_bytes != 0) {
        ok = reader(first, first_bytes);
    }
    if (ok && second_bytes != 0) {
        ok = reader(second, second_bytes);
    }

    buffer->Unlock(first, first_bytes, second, second_bytes);
    return ok;
}

template <typename Upload>
u32 create_wave_slot_and_upload(const PcmWaveInfo& info, Upload&& upload) {
    if (!g_direct_sound_state.active || g_direct_sound_state.direct_sound == nullptr) {
        return kInvalidDirectSoundBufferSlot;
    }

    const u32 slot = AllocateDirectSoundBufferSlotIndex();
    if (slot == kInvalidDirectSoundBufferSlot) {
        return kInvalidDirectSoundBufferSlot;
    }

    const bool built = BuildSecondarySoundBufferSlot(slot, info.data_bytes, info.sample_rate,
        info.bits_per_sample, info.block_align, info.channels,
        g_direct_sound_state.secondary_buffer_extra_flags);
    if (!built) {
        ReleaseDirectSoundBufferSlotsFrom(slot);
        return kInvalidDirectSoundBufferSlot;
    }

    auto* buffer = direct_sound_buffer_slot(slot);
    if (buffer == nullptr || !upload(buffer, info.data_bytes)) {
        ReleaseDirectSoundBufferSlot(slot);
        ReleaseDirectSoundBufferSlotsFrom(slot);
        return kInvalidDirectSoundBufferSlot;
    }
    return slot;
}

HRESULT create_direct_sound_buffer_slot_impl(u32 slot_index, DWORD buffer_bytes,
    DWORD sample_rate, WORD bits_per_sample, WORD channels, DWORD extra_flags,
    bool release_existing) {
    ensure_direct_sound_slots();
    if (!g_direct_sound_state.active) {
        return g_direct_sound_state.last_result;
    }
    if (g_direct_sound_state.direct_sound == nullptr ||
        slot_index >= g_direct_sound_state.secondary_buffers.size()) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    if (release_existing) {
        ReleaseDirectSoundBufferSlot(slot_index);
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = bits_per_sample;
    format.nBlockAlign = static_cast<WORD>((bits_per_sample / 8) * channels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = (extra_flags | 0x000000c8u);
    desc.dwBufferBytes = buffer_bytes;
    desc.lpwfxFormat = &format;

    HRESULT result = g_direct_sound_state.direct_sound->CreateSoundBuffer(
        &desc, &g_direct_sound_state.secondary_buffers[slot_index], nullptr);
    g_direct_sound_state.last_result = result;
    if (result != DS_OK) {
        g_direct_sound_state.secondary_buffers[slot_index] = nullptr;
    }
    return result;
}

void ensure_direct_sound_slots() {
    if (g_direct_sound_state.secondary_buffers.size() != kMaxDirectSoundBufferSlots) {
        g_direct_sound_state.secondary_buffers.assign(kMaxDirectSoundBufferSlots, nullptr);
    }
}

HRESULT create_primary_surface() {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    return g_direct_draw_state.direct_draw->CreateSurface(
        &desc, &g_direct_draw_state.primary_surface, nullptr);
}

HRESULT create_back_surface(int width, int height) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = static_cast<DWORD>(width);
    desc.dwHeight = static_cast<DWORD>(height);
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 16;
    desc.ddpfPixelFormat.dwRBitMask = 0xf800;
    desc.ddpfPixelFormat.dwGBitMask = 0x07e0;
    desc.ddpfPixelFormat.dwBBitMask = 0x001f;

    HRESULT result = g_direct_draw_state.direct_draw->CreateSurface(
        &desc, &g_direct_draw_state.back_surface, nullptr);
    if (SUCCEEDED(result)) {
        return result;
    }

    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    std::memset(&desc.ddpfPixelFormat, 0, sizeof(desc.ddpfPixelFormat));
    return g_direct_draw_state.direct_draw->CreateSurface(
        &desc, &g_direct_draw_state.back_surface, nullptr);
}

RECT full_direct_draw_surface_rect() {
    RECT rect{};
    rect.left = 0;
    rect.top = 0;
    rect.right = static_cast<LONG>(g_direct_draw_state.width);
    rect.bottom = static_cast<LONG>(g_direct_draw_state.height);
    return rect;
}

HRESULT create_surface_snapshot(LPDIRECTDRAWSURFACE7& surface) {
    if (g_direct_draw_state.direct_draw == nullptr) {
        return DDERR_GENERIC;
    }

    release_com(surface);

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = g_direct_draw_state.width;
    desc.dwHeight = g_direct_draw_state.height;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;

    return g_direct_draw_state.direct_draw->CreateSurface(&desc, &surface, nullptr);
}

bool empty_rect(const RECT& rect) {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

HRESULT blt_fast_surface_region(LPDIRECTDRAWSURFACE7 dest, DWORD x, DWORD y,
    LPDIRECTDRAWSURFACE7 source, RECT& source_rect) {
    if (dest == nullptr || source == nullptr) {
        return DDERR_GENERIC;
    }
    if (empty_rect(source_rect)) {
        return DD_OK;
    }

    return dest->BltFast(x, y, source, &source_rect, DDBLTFAST_NOCOLORKEY);
}

HRESULT blt_fast_snapshot_region(LPDIRECTDRAWSURFACE7 dest, DWORD x, DWORD y,
    LPDIRECTDRAWSURFACE7 source) {
    RECT source_rect = full_direct_draw_surface_rect();
    return blt_fast_surface_region(dest, x, y, source, source_rect);
}

RECT active_region_copy_rect() {
    if (g_direct_draw_state.region_copy_rect_set) {
        return g_direct_draw_state.region_copy_rect;
    }
    return full_direct_draw_surface_rect();
}

HRESULT copy_direct_draw_region(LPDIRECTDRAWSURFACE7 dest, LPDIRECTDRAWSURFACE7 source) {
    if (!g_direct_draw_state.active) {
        return g_direct_draw_state.last_result;
    }
    if (dest == nullptr || source == nullptr) {
        g_direct_draw_state.last_result = DDERR_GENERIC;
        UpdateDirectDrawErrorString(g_direct_draw_state.last_result);
        return g_direct_draw_state.last_result;
    }

    RECT source_rect = active_region_copy_rect();
    const HRESULT result = blt_fast_surface_region(dest,
        static_cast<DWORD>(g_direct_draw_state.region_copy_destination.x),
        static_cast<DWORD>(g_direct_draw_state.region_copy_destination.y),
        source, source_rect);
    g_direct_draw_state.last_result = result;
    UpdateDirectDrawErrorString(result);
    return result;
}

void release_surface_snapshot_at(u32 index) {
    if (index >= g_direct_draw_state.surface_snapshots.size()) {
        return;
    }
    release_com(g_direct_draw_state.surface_snapshots[static_cast<std::size_t>(index)]);
}

void release_all_surface_snapshots() {
    while (g_direct_draw_state.surface_snapshot_depth != 0) {
        --g_direct_draw_state.surface_snapshot_depth;
        release_surface_snapshot_at(g_direct_draw_state.surface_snapshot_depth);
    }
}

HRESULT push_surface_snapshot(LPDIRECTDRAWSURFACE7 source, bool hide_visible_cursor) {
    if (!g_direct_draw_state.active || source == nullptr ||
        g_direct_draw_state.surface_snapshot_depth >= kDirectDrawSurfaceSnapshotCount) {
        g_direct_draw_state.last_result = DDERR_GENERIC;
        UpdateDirectDrawErrorString(g_direct_draw_state.last_result);
        return g_direct_draw_state.last_result;
    }

    const u32 slot = g_direct_draw_state.surface_snapshot_depth;
    auto& snapshot = g_direct_draw_state.surface_snapshots[static_cast<std::size_t>(slot)];
    HRESULT result = create_surface_snapshot(snapshot);
    if (SUCCEEDED(result)) {
        const bool cursor_was_visible = hide_visible_cursor && software_cursor_state().visible;
        if (cursor_was_visible) {
            HideGameCursor();
        }

        result = blt_fast_snapshot_region(snapshot, 0, 0, source);

        if (cursor_was_visible) {
            ShowGameCursor();
        }

        if (g_direct_draw_state.surface_snapshot_depth <
            kOriginalDirectDrawSurfaceSnapshotMaxDepth) {
            ++g_direct_draw_state.surface_snapshot_depth;
        }
    }

    g_direct_draw_state.last_result = result;
    UpdateDirectDrawErrorString(result);
    return result;
}

HRESULT pop_surface_snapshot(LPDIRECTDRAWSURFACE7 dest, bool hide_visible_cursor) {
    if (g_direct_draw_state.surface_snapshot_depth == 0) {
        return DD_OK;
    }
    if (!g_direct_draw_state.active || dest == nullptr) {
        g_direct_draw_state.last_result = DDERR_GENERIC;
        UpdateDirectDrawErrorString(g_direct_draw_state.last_result);
        return g_direct_draw_state.last_result;
    }

    --g_direct_draw_state.surface_snapshot_depth;
    const u32 slot = g_direct_draw_state.surface_snapshot_depth;
    auto* snapshot = g_direct_draw_state.surface_snapshots[static_cast<std::size_t>(slot)];

    const bool cursor_was_visible = hide_visible_cursor && software_cursor_state().visible;
    if (cursor_was_visible) {
        HideGameCursor();
    }

    const HRESULT result = blt_fast_snapshot_region(dest, 0, 0, snapshot);

    if (cursor_was_visible) {
        ShowGameCursor();
    }

    release_surface_snapshot_at(slot);
    g_direct_draw_state.last_result = result;
    UpdateDirectDrawErrorString(result);
    return result;
}

HRESULT attach_window_clipper(HWND window) {
    HRESULT result = g_direct_draw_state.direct_draw->CreateClipper(
        0, &g_direct_draw_state.clipper, nullptr);
    if (FAILED(result)) {
        return result;
    }

    result = g_direct_draw_state.clipper->SetHWnd(0, window);
    if (FAILED(result)) {
        return result;
    }

    return g_direct_draw_state.primary_surface->SetClipper(g_direct_draw_state.clipper);
}

void refresh_windowed_presentation_rect(HWND window, int logical_width,
    int logical_height) {
    SetRect(&g_direct_draw_state.client_rect, 0, 0,
        logical_width, logical_height);

    RECT destination{};
    if (!GetClientRect(window, &destination)) {
        destination = g_direct_draw_state.client_rect;
    }
    POINT top_left{destination.left, destination.top};
    POINT bottom_right{destination.right, destination.bottom};
    ClientToScreen(window, &top_left);
    ClientToScreen(window, &bottom_right);
    g_direct_draw_state.screen_rect = RECT{
        top_left.x, top_left.y, bottom_right.x, bottom_right.y};
}

HRESULT configure_direct_draw_surfaces(HWND window, int width, int height, int color_depth,
    bool windowed) {
    g_direct_draw_state.active = false;

    if (g_direct_draw_state.direct_draw == nullptr) {
        return DDERR_GENERIC;
    }

    release_all_surface_snapshots();
    release_com(g_direct_draw_state.back_surface);
    release_com(g_direct_draw_state.primary_surface);
    release_com(g_direct_draw_state.clipper);

    HRESULT result = DD_OK;
    if (windowed) {
        result = g_direct_draw_state.direct_draw->SetCooperativeLevel(window, DDSCL_NORMAL);
        if (FAILED(result)) {
            return result;
        }

        result = create_primary_surface();
        if (FAILED(result)) {
            return result;
        }

        // The source rectangle is always the logical DirectDraw surface.  The
        // destination is the independently sized client rectangle in screen
        // coordinates, matching cnc-ddraw's original stretch presentation.
        refresh_windowed_presentation_rect(window, width, height);

        result = attach_window_clipper(window);
        if (FAILED(result)) {
            return result;
        }
        release_com(g_direct_draw_state.clipper);

        result = create_back_surface(width, height);
        if (FAILED(result)) {
            return result;
        }
    }
    else {
        result = g_direct_draw_state.direct_draw->SetCooperativeLevel(
            window, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE);
        if (FAILED(result)) {
            return result;
        }

        result = g_direct_draw_state.direct_draw->SetDisplayMode(
            width, height, color_depth, 0, 0);
        if (FAILED(result)) {
            return result;
        }

        SetRect(&g_direct_draw_state.client_rect, 0, 0, width, height);
        g_direct_draw_state.screen_rect = g_direct_draw_state.client_rect;

        result = create_primary_surface();
        if (FAILED(result)) {
            return result;
        }

        // The original refreshes DAT_0143ff48 here, but fullscreen presentation
        // keeps using the display-mode-sized DAT_0143ff38 source rectangle.
        GetClientRect(window, &g_direct_draw_state.screen_rect);
        result = create_back_surface(width, height);
        if (FAILED(result)) {
            return result;
        }
    }

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    u32 actual_color_depth = static_cast<u32>(color_depth);
    result = g_direct_draw_state.back_surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
    if (SUCCEEDED(result)) {
        g_direct_draw_state.red_mask = desc.ddpfPixelFormat.dwRBitMask;
        g_direct_draw_state.green_mask = desc.ddpfPixelFormat.dwGBitMask;
        g_direct_draw_state.blue_mask = desc.ddpfPixelFormat.dwBBitMask;
        if (desc.ddpfPixelFormat.dwRGBBitCount != 0) {
            actual_color_depth = desc.ddpfPixelFormat.dwRGBBitCount;
        }
        g_direct_draw_state.pixel_mode_555 =
            desc.ddpfPixelFormat.dwGBitMask == 0x07e0 ? 0 : 1;
        g_direct_draw_state.back_surface->Unlock(nullptr);
        RefreshPaletteTransparentMask();
        BuildPixelBlendTables();
        ConfigureSpritePixelMaskConstants(g_direct_draw_state.pixel_mode_555 != 0);
    }

    const u32 bytes_per_pixel = static_cast<u32>(std::max<u32>(actual_color_depth, 8) / 8);
    g_direct_draw_state.scanline_offsets.resize(static_cast<std::size_t>(height));
    u32 offset = 0;
    for (int y = 0; y < height; ++y) {
        g_direct_draw_state.scanline_offsets[static_cast<std::size_t>(y)] = offset;
        offset += static_cast<u32>(width) * bytes_per_pixel;
    }
    g_direct_draw_state.row_stride_words = (static_cast<u32>(width) * bytes_per_pixel) >> 1;
    g_direct_draw_state.width = static_cast<u32>(width);
    g_direct_draw_state.height = static_cast<u32>(height);
    g_direct_draw_state.color_depth = actual_color_depth;
    g_direct_draw_state.windowed = windowed;
    g_direct_draw_state.active = true;
    return DD_OK;
}

} // namespace

HRESULT InitDirectDrawSubsystem(HWND window, int width, int height, int color_depth,
    bool windowed) {
    HRESULT result = DirectDrawCreateEx(
        nullptr, reinterpret_cast<void**>(&g_direct_draw_state.direct_draw),
        IID_IDirectDraw7, nullptr);
    g_direct_draw_state.last_result = result;
    if (FAILED(result)) {
        UpdateDirectDrawErrorString(result);
        return result;
    }

    result = configure_direct_draw_surfaces(window, width, height, color_depth, windowed);
    g_direct_draw_state.last_result = result;
    if (FAILED(result)) {
        UpdateDirectDrawErrorString(result);
        ShutdownDirectDrawSubsystem(window);
    }
    return result;
}

HRESULT ConfigureDirectDrawSurfaces(HWND window, int width, int height, int color_depth) {
    return ConfigureDirectDrawSurfaces(window, width, height, color_depth,
        g_direct_draw_state.windowed);
}

HRESULT ConfigureDirectDrawSurfaces(HWND window, int width, int height, int color_depth,
    bool windowed) {
    const HRESULT result = configure_direct_draw_surfaces(window, width, height, color_depth,
        windowed);
    g_direct_draw_state.last_result = result;
    if (FAILED(result)) {
        ShutdownDirectDrawSubsystem(window);
    }
    return result;
}

void ShutdownDirectDrawSubsystem(HWND window) {
    if (g_direct_draw_state.direct_draw != nullptr) {
        g_direct_draw_state.direct_draw->SetCooperativeLevel(window, DDSCL_NORMAL);
    }

    release_com(g_direct_draw_state.clipper);
    release_com(g_direct_draw_state.back_surface);
    release_com(g_direct_draw_state.primary_surface);
    g_direct_draw_state.active = false;
}

void RefreshDirectDrawPresentationRect(HWND window) {
    if (!g_direct_draw_state.windowed || window == nullptr) {
        return;
    }
    refresh_windowed_presentation_rect(window,
        static_cast<int>(g_direct_draw_state.width),
        static_cast<int>(g_direct_draw_state.height));
}

HRESULT PresentBackBufferToPrimary() {
    if (!g_direct_draw_state.active || g_direct_draw_state.primary_surface == nullptr ||
        g_direct_draw_state.back_surface == nullptr) {
        return g_direct_draw_state.last_result;
    }

    HRESULT result = DD_OK;
    do {
        if (!g_direct_draw_state.windowed) {
            result = g_direct_draw_state.primary_surface->BltFast(
                0, 0, g_direct_draw_state.back_surface, &g_direct_draw_state.client_rect,
                DDBLTFAST_WAIT);
        }
        else {
            result = g_direct_draw_state.primary_surface->Blt(
                &g_direct_draw_state.screen_rect, g_direct_draw_state.back_surface,
                &g_direct_draw_state.client_rect, DDBLT_WAIT, nullptr);
        }

        if (result == DDERR_SURFACELOST) {
            const HRESULT restore_result = g_direct_draw_state.primary_surface->Restore();
            result = restore_result;
            if (FAILED(restore_result)) {
                break;
            }
        }
    } while (result == DDERR_WASSTILLDRAWING);

    g_direct_draw_state.last_result = result;
    return result;
}

HRESULT LockBackBufferSpriteRenderTarget(SpriteRenderTarget& target) {
    target = SpriteRenderTarget{};
    if (!g_direct_draw_state.active || g_direct_draw_state.back_surface == nullptr) {
        g_direct_draw_state.last_result = DDERR_GENERIC;
        return g_direct_draw_state.last_result;
    }
    if (g_direct_draw_state.color_depth != 16) {
        g_direct_draw_state.last_result = DDERR_UNSUPPORTEDFORMAT;
        return g_direct_draw_state.last_result;
    }

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    HRESULT result = DDERR_GENERIC;
    for (int attempt = 0; attempt < 2; ++attempt) {
        result = g_direct_draw_state.back_surface->Lock(nullptr, &desc, DDLOCK_WAIT,
            nullptr);
        if (result != DDERR_SURFACELOST) {
            break;
        }
        const HRESULT restore_result = g_direct_draw_state.back_surface->Restore();
        if (FAILED(restore_result)) {
            g_direct_draw_state.last_result = restore_result;
            return restore_result;
        }
    }

    if (FAILED(result)) {
        g_direct_draw_state.last_result = result;
        return result;
    }
    if (desc.lpSurface == nullptr) {
        g_direct_draw_state.back_surface->Unlock(nullptr);
        g_direct_draw_state.last_result = DDERR_GENERIC;
        return g_direct_draw_state.last_result;
    }

    target.pixels = static_cast<u16*>(desc.lpSurface);
    target.width = g_direct_draw_state.width;
    target.height = g_direct_draw_state.height;
    target.stride_words = static_cast<u32>(desc.lPitch / sizeof(u16));
    g_direct_draw_state.last_result = DD_OK;
    return g_direct_draw_state.last_result;
}

HRESULT UnlockBackBufferSpriteRenderTarget() {
    if (!g_direct_draw_state.active || g_direct_draw_state.back_surface == nullptr) {
        g_direct_draw_state.last_result = DDERR_GENERIC;
        return g_direct_draw_state.last_result;
    }
    g_direct_draw_state.last_result =
        g_direct_draw_state.back_surface->Unlock(nullptr);
    return g_direct_draw_state.last_result;
}

void SetDirectDrawRegionCopyRect(const RECT& source_rect, LONG dest_x, LONG dest_y) {
    g_direct_draw_state.region_copy_rect = source_rect;
    g_direct_draw_state.region_copy_destination.x = dest_x;
    g_direct_draw_state.region_copy_destination.y = dest_y;
    g_direct_draw_state.region_copy_rect_set = true;
}

HRESULT CopyPrimaryRegionToBackBuffer() {
    return copy_direct_draw_region(g_direct_draw_state.back_surface,
        g_direct_draw_state.primary_surface);
}

HRESULT CopyBackBufferRegionToPrimary() {
    return copy_direct_draw_region(g_direct_draw_state.primary_surface,
        g_direct_draw_state.back_surface);
}

void HandlePrimarySurfaceLostRefresh() {
    if (!g_direct_draw_state.active || g_direct_draw_state.primary_surface == nullptr) {
        return;
    }

    HRESULT result = g_direct_draw_state.primary_surface->IsLost();
    if (result == DDERR_SURFACELOST) {
        g_direct_draw_state.primary_surface->Restore();
        HandleCursorAwarePresentForwarder();
    }
}

void UpdateDirectDrawErrorString(HRESULT result) {
    g_direct_draw_state.last_result = result;
    g_direct_draw_state.last_error_name = direct_draw_error_name(result);
}

void UpdateDirectDrawErrorString() {
    UpdateDirectDrawErrorString(g_direct_draw_state.last_result);
}

void PushPrimarySurfaceSnapshot() {
    push_surface_snapshot(g_direct_draw_state.primary_surface, true);
}

void PushBackSurfaceSnapshot() {
    push_surface_snapshot(g_direct_draw_state.back_surface, false);
}

void PopPrimarySurfaceSnapshot() {
    pop_surface_snapshot(g_direct_draw_state.primary_surface, true);
}

void PopBackSurfaceSnapshot() {
    pop_surface_snapshot(g_direct_draw_state.back_surface, false);
}

void ReleaseTopDirectDrawSurfaceSnapshot() {
    if (g_direct_draw_state.surface_snapshot_depth == 0) {
        return;
    }

    --g_direct_draw_state.surface_snapshot_depth;
    release_surface_snapshot_at(g_direct_draw_state.surface_snapshot_depth);
}

void ReleaseAllDirectDrawSurfaceSnapshots() {
    release_all_surface_snapshots();
}

void BuildPixelBlendTables() {
    BuildSpriteBlendTables(g_direct_draw_state.pixel_mode_555 != 0);
}

HRESULT HandleDirectDrawFrameBoundary() {
    HRESULT result = fill_direct_draw_surface_black(g_direct_draw_state.back_surface);
    if (SUCCEEDED(result)) {
        result = fill_direct_draw_surface_black(g_direct_draw_state.primary_surface);
    }
    if (SUCCEEDED(result)) {
        ++g_bink_video_state.surface_clear_count;
    }
    g_direct_draw_state.last_result = result;
    return result;
}

void HandleBackBufferFadeToBlack(u32 steps) {
    for (u32 i = 0; i < steps; ++i) {
        const HRESULT result = darken_back_surface_once();
        g_direct_draw_state.last_result = result;
        if (FAILED(result)) {
            break;
        }
        ++g_bink_video_state.fade_steps;
        PresentBackBufferToPrimary();
    }
}

void HandleBackBufferFadeFromBlack(u32 steps) {
    if (!g_direct_draw_state.active || g_direct_draw_state.back_surface == nullptr ||
        g_direct_draw_state.color_depth != 16 || steps == 0) {
        return;
    }

    SpriteRenderTarget target{};
    HRESULT result = LockBackBufferSpriteRenderTarget(target);
    if (FAILED(result)) {
        g_direct_draw_state.last_result = result;
        return;
    }

    const u32 width = target.width;
    const u32 height = target.height;
    std::vector<u16> original(static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height));
    for (u32 y = 0; y < height; ++y) {
        auto* row = target.pixels + static_cast<std::size_t>(y) * target.stride_words;
        std::memcpy(original.data() + static_cast<std::size_t>(y) * width, row,
            static_cast<std::size_t>(width) * sizeof(u16));
        std::memset(row, 0, static_cast<std::size_t>(width) * sizeof(u16));
    }
    result = UnlockBackBufferSpriteRenderTarget();
    if (FAILED(result)) {
        g_direct_draw_state.last_result = result;
        return;
    }

    u32 red_mask = g_direct_draw_state.red_mask != 0 ? g_direct_draw_state.red_mask : 0x7c00;
    u32 green_mask =
        g_direct_draw_state.green_mask != 0 ? g_direct_draw_state.green_mask : 0x03e0;
    u32 blue_mask =
        g_direct_draw_state.blue_mask != 0 ? g_direct_draw_state.blue_mask : 0x001f;
    u32 red_step = lowest_component_bit(red_mask);
    u32 green_step = lowest_component_bit(green_mask);
    u32 blue_step = lowest_component_bit(blue_mask);
    if (green_mask == 0x07e0) {
        green_mask = 0x07c0;
        green_step = 0x0040;
    }

    u32 red_limit = 0;
    u32 green_limit = 0;
    u32 blue_limit = 0;
    for (u32 step = 0; step < steps; ++step) {
        target = {};
        result = LockBackBufferSpriteRenderTarget(target);
        if (FAILED(result)) {
            g_direct_draw_state.last_result = result;
            return;
        }
        for (u32 y = 0; y < height; ++y) {
            auto* row = target.pixels + static_cast<std::size_t>(y) * target.stride_words;
            const u16* src =
                original.data() + static_cast<std::size_t>(y) * width;
            for (u32 x = 0; x < width; ++x) {
                row[x] = limit_16bit_pixel(src[x], red_mask, green_mask, blue_mask,
                    red_limit, green_limit, blue_limit);
            }
        }
        result = UnlockBackBufferSpriteRenderTarget();
        if (FAILED(result)) {
            g_direct_draw_state.last_result = result;
            return;
        }
        PresentBackBufferToPrimary();
        red_limit = std::min(red_limit + red_step, red_mask);
        green_limit = std::min(green_limit + green_step, green_mask);
        blue_limit = std::min(blue_limit + blue_step, blue_mask);
        ++g_bink_video_state.fade_steps;
    }
}

HRESULT FillPrimaryDirectDrawSurfaceBlack() {
    const HRESULT result = fill_direct_draw_surface_black(g_direct_draw_state.primary_surface);
    g_direct_draw_state.last_result = result;
    return result;
}

HRESULT FillBackDirectDrawSurfaceBlack() {
    const HRESULT result = fill_direct_draw_surface_black(g_direct_draw_state.back_surface);
    g_direct_draw_state.last_result = result;
    return result;
}

void SendTrcRecordFatalErrorMessage(HWND window, const char* archive_name,
    u32 record_index) {
    SendMessageA(window, WM_USER + 1, static_cast<WPARAM>(record_index),
        reinterpret_cast<LPARAM>(archive_name));
}

void SendSetupWriteErrorMessage(HWND window, const char* path) {
    SendMessageA(window, WM_USER + 2, 0, reinterpret_cast<LPARAM>(path));
}

void SendGenericFatalErrorMessage(HWND window, const char* detail) {
    SendMessageA(window, WM_USER + 3, 0, reinterpret_cast<LPARAM>(detail));
}

void SendWorkerModalPauseMessage(HWND window) {
    SendMessageA(window, WM_USER + 4, 0, 0);
}

void SendWorkerModalResumeMessage(HWND window) {
    SendMessageA(window, WM_USER + 5, 0, 0);
}

LRESULT SendExternalLaunchMessage(HWND window, const char* parameters) {
    return SendMessageA(window, WM_USER + 6, 0, reinterpret_cast<LPARAM>(parameters));
}

void SendFrontendCallbackMessage(HWND window) {
    SendMessageA(window, WM_USER + 7, 0, 0);
}

LRESULT SendFrontendGameModalMessage(HWND window, u32 action) {
    return SendMessageA(window, WM_USER + 8, 0, static_cast<LPARAM>(action));
}

LRESULT SendP2PGameFlowModalMessage(HWND window) {
    return SendFrontendGameModalMessage(window, 2);
}

LRESULT SendFrontendGameModalResumeMessage(HWND window, u32* modal_wait_flag) {
    if (modal_wait_flag != nullptr) {
        *modal_wait_flag = 0;
    }
    return SendFrontendGameModalMessage(window, 1);
}

LRESULT SendReplayModalMessage(HWND window, u32 action) {
    return SendMessageA(window, WM_USER + 9, 0, static_cast<LPARAM>(action));
}

LRESULT SendFrontendNetworkRouteMessage(HWND window, WPARAM wparam, LPARAM lparam) {
    return SendMessageA(window, WM_USER + 0x65, wparam, lparam);
}

void PaintMainWindowBlack(HWND window) {
    if (window == nullptr) {
        return;
    }

    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    if (dc != nullptr) {
        PatBlt(dc, 0, 0, 0x1000, 0x1000, BLACKNESS);
    }
    EndPaint(window, &paint);
}

bool InitDirectSoundSubsystem(HWND window) {
    g_direct_sound_state.active = false;
    ensure_direct_sound_slots();
    for (auto*& buffer : g_direct_sound_state.secondary_buffers) {
        buffer = nullptr;
    }

    HRESULT result = DirectSoundCreate(nullptr, &g_direct_sound_state.direct_sound, nullptr);
    g_direct_sound_state.last_result = result;
    if (FAILED(result)) {
        ShutdownDirectSoundSubsystem();
        return false;
    }

    result = g_direct_sound_state.direct_sound->SetCooperativeLevel(window, DSSCL_PRIORITY);
    g_direct_sound_state.last_result = result;
    if (FAILED(result)) {
        ShutdownDirectSoundSubsystem();
        return false;
    }

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    result = g_direct_sound_state.direct_sound->CreateSoundBuffer(
        &desc, &g_direct_sound_state.primary_buffer, nullptr);
    g_direct_sound_state.last_result = result;
    if (FAILED(result)) {
        ShutdownDirectSoundSubsystem();
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 22050;
    format.wBitsPerSample = 16;
    format.nBlockAlign =
        static_cast<WORD>((format.wBitsPerSample / 8) * format.nChannels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    g_direct_sound_state.primary_buffer->SetFormat(&format);

    result = g_direct_sound_state.primary_buffer->Play(0, 0, DSBPLAY_LOOPING);
    g_direct_sound_state.last_result = result;
    if (FAILED(result)) {
        ShutdownDirectSoundSubsystem();
        return false;
    }

    g_direct_sound_state.average_bytes_per_second = format.nAvgBytesPerSec;
    ResetDirectSoundReservedBuffers();
    g_direct_sound_state.active = true;
    return true;
}

void ShutdownDirectSoundSubsystem() {
    if (g_direct_sound_state.direct_sound != nullptr) {
        if (g_direct_sound_state.primary_buffer != nullptr) {
            ReleaseReservedDirectSoundBuffers();
            ensure_direct_sound_slots();
            for (u32 slot_index = 0; slot_index < kMaxDirectSoundBufferSlots; ++slot_index) {
                ReleaseDirectSoundBufferSlot(slot_index);
            }

            g_direct_sound_state.primary_buffer->Stop();
            release_com(g_direct_sound_state.primary_buffer);
        }

        release_com(g_direct_sound_state.direct_sound);
    }
    g_direct_sound_state.active = false;
}

bool BuildSecondarySoundBufferSlot(u32 slot_index, DWORD buffer_bytes, DWORD sample_rate,
    WORD bits_per_sample, WORD block_align, WORD channels, DWORD extra_flags) {
    (void)block_align;
    if (!g_direct_sound_state.active) {
        return false;
    }

    const HRESULT result = create_direct_sound_buffer_slot_impl(slot_index, buffer_bytes,
        sample_rate, bits_per_sample, channels, extra_flags, false);
    return result == DS_OK;
}

HRESULT CreateDirectSoundBufferSlot(u32 slot_index, DWORD buffer_bytes, DWORD sample_rate,
    WORD bits_per_sample, WORD channels, DWORD extra_flags) {
    const WORD block_align = static_cast<WORD>((channels * bits_per_sample) / 8);
    return CreateDirectSoundBufferSlot(slot_index, buffer_bytes, sample_rate, bits_per_sample,
        block_align, channels, extra_flags);
}

HRESULT CreateDirectSoundBufferSlot(u32 slot_index, DWORD buffer_bytes, DWORD sample_rate,
    WORD bits_per_sample, WORD block_align, WORD channels, DWORD extra_flags) {
    (void)block_align;
    return create_direct_sound_buffer_slot_impl(slot_index, buffer_bytes, sample_rate,
        bits_per_sample, channels, extra_flags, true);
}

void ReleaseDirectSoundBufferSlot(u32 slot_index) {
    if (slot_index >= g_direct_sound_state.secondary_buffers.size()) {
        return;
    }
    if (g_direct_sound_state.secondary_buffers[slot_index] != nullptr) {
        g_direct_sound_state.secondary_buffers[slot_index]->Stop();
    }
    release_com(g_direct_sound_state.secondary_buffers[slot_index]);
}

void ResetDirectSoundReservedBuffers() {
    for (u32 index = 0; index < g_direct_sound_state.reserved_buffers.size(); ++index) {
        g_direct_sound_state.reserved_buffer_scan_index = index;
        g_direct_sound_state.reserved_buffers[index] = nullptr;
    }
    g_direct_sound_state.reserved_buffer_scan_index =
        static_cast<u32>(g_direct_sound_state.reserved_buffers.size());
}

void ReleaseReservedDirectSoundBuffers() {
    for (u32 index = 0; index < g_direct_sound_state.reserved_buffers.size(); ++index) {
        g_direct_sound_state.reserved_buffer_scan_index = index;
        auto*& buffer = g_direct_sound_state.reserved_buffers[index];
        if (buffer != nullptr) {
            buffer->Stop();
        }
        release_com(buffer);
    }
    g_direct_sound_state.reserved_buffer_scan_index =
        static_cast<u32>(g_direct_sound_state.reserved_buffers.size());
}

void ReleaseStoppedReservedSoundBuffers() {
    ReleaseStoppedReservedDirectSoundBuffers();
}

void ReleaseStoppedReservedDirectSoundBuffers() {
    for (u32 index = 0; index < g_direct_sound_state.reserved_buffers.size(); ++index) {
        g_direct_sound_state.reserved_buffer_scan_index = index;
        auto*& buffer = g_direct_sound_state.reserved_buffers[index];
        if (buffer == nullptr) {
            continue;
        }

        DWORD status = 0;
        if (SUCCEEDED(buffer->GetStatus(&status)) &&
            (status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) == 0) {
            release_com(buffer);
        }
    }
    g_direct_sound_state.reserved_buffer_scan_index =
        static_cast<u32>(g_direct_sound_state.reserved_buffers.size());
}

bool HasFreeReservedSoundBuffer() {
    return HasFreeReservedDirectSoundBuffer();
}

bool HasFreeReservedDirectSoundBuffer() {
    for (u32 index = 0; index < g_direct_sound_state.reserved_buffers.size(); ++index) {
        g_direct_sound_state.reserved_buffer_scan_index = index;
        if (g_direct_sound_state.reserved_buffers[index] == nullptr) {
            return true;
        }
    }
    g_direct_sound_state.reserved_buffer_scan_index =
        static_cast<u32>(g_direct_sound_state.reserved_buffers.size());
    return false;
}

void SetCurrentDirectSoundBufferSlotIndex(u32 slot_index) {
    g_direct_sound_state.current_slot_index = slot_index;
}

void SetCurrentDirectSoundBufferPlaybackState(u32 slot_index, LONG volume, LONG pan) {
    g_direct_sound_state.current_slot_index = slot_index;
    g_direct_sound_state.last_volume = volume;
    g_direct_sound_state.last_pan = pan;
}

HRESULT PlayDirectSoundBufferSlot(u32 slot_index, DWORD play_flags) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    if (!g_direct_sound_state.active) {
        return g_direct_sound_state.last_result;
    }

    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.play_flags = play_flags;
    g_direct_sound_state.last_result = buffer->Play(0, 0, play_flags);
    return g_direct_sound_state.last_result;
}

void PlayCurrentSoundBufferSlot() {
    PlayDirectSoundBufferSlot(g_direct_sound_state.current_slot_index,
        g_direct_sound_state.play_flags);
}

HRESULT DuplicateAndPlayReservedDirectSoundBuffer(u32 slot_index) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    if (!g_direct_sound_state.active) {
        return g_direct_sound_state.last_result;
    }

    auto* source = direct_sound_buffer_slot(slot_index);
    if (g_direct_sound_state.direct_sound == nullptr || source == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    ReleaseStoppedReservedSoundBuffers();
    if (!HasFreeReservedSoundBuffer()) {
        return g_direct_sound_state.last_result;
    }

    const u32 target_index = g_direct_sound_state.reserved_buffer_scan_index;
    if (target_index >= g_direct_sound_state.reserved_buffers.size()) {
        return g_direct_sound_state.last_result;
    }

    auto*& target = g_direct_sound_state.reserved_buffers[target_index];
    HRESULT result = g_direct_sound_state.direct_sound->DuplicateSoundBuffer(source, &target);
    g_direct_sound_state.last_result = result;
    if (FAILED(result)) {
        return result;
    }
    result = target->Play(0, 0, 0);
    g_direct_sound_state.last_result = result;
    return result;
}

void DuplicateAndPlayReservedSoundBuffer() {
    DuplicateAndPlayReservedDirectSoundBuffer(g_direct_sound_state.current_slot_index);
}

HRESULT StopDirectSoundBufferSlot(u32 slot_index) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    if (!g_direct_sound_state.active) {
        return g_direct_sound_state.last_result;
    }

    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.last_result = buffer->Stop();
    return g_direct_sound_state.last_result;
}

void StopCurrentSoundBufferSlot() {
    StopDirectSoundBufferSlot(g_direct_sound_state.current_slot_index);
}

void ReleaseCurrentSoundBufferSlot() {
    ReleaseDirectSoundBufferSlot(g_direct_sound_state.current_slot_index);
}

HRESULT GetDirectSoundBufferSlotStatus(u32 slot_index, DWORD* status) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.last_result = buffer->GetStatus(&g_direct_sound_state.last_status);
    if (status != nullptr) {
        *status = g_direct_sound_state.last_status;
    }
    return g_direct_sound_state.last_result;
}

void GetCurrentSoundBufferStatus() {
    GetDirectSoundBufferSlotStatus(g_direct_sound_state.current_slot_index,
        &g_direct_sound_state.last_status);
}

HRESULT GetDirectSoundBufferSlotFrequency(u32 slot_index, DWORD* frequency) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.last_result =
        buffer->GetFrequency(&g_direct_sound_state.last_frequency);
    if (frequency != nullptr) {
        *frequency = g_direct_sound_state.last_frequency;
    }
    return g_direct_sound_state.last_result;
}

void GetCurrentSoundBufferFrequency() {
    GetDirectSoundBufferSlotFrequency(g_direct_sound_state.current_slot_index,
        &g_direct_sound_state.last_frequency);
}

HRESULT GetDirectSoundBufferSlotVolume(u32 slot_index, LONG* volume) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.last_result = buffer->GetVolume(&g_direct_sound_state.last_volume);
    if (volume != nullptr) {
        *volume = g_direct_sound_state.last_volume;
    }
    return g_direct_sound_state.last_result;
}

void GetCurrentSoundBufferVolume() {
    GetDirectSoundBufferSlotVolume(g_direct_sound_state.current_slot_index,
        &g_direct_sound_state.last_volume);
}

HRESULT GetDirectSoundBufferSlotPan(u32 slot_index, LONG* pan) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.last_result = buffer->GetPan(&g_direct_sound_state.last_pan);
    if (pan != nullptr) {
        *pan = g_direct_sound_state.last_pan;
    }
    return g_direct_sound_state.last_result;
}

void GetCurrentSoundBufferPan() {
    GetDirectSoundBufferSlotPan(g_direct_sound_state.current_slot_index,
        &g_direct_sound_state.last_pan);
}

HRESULT SetDirectSoundBufferSlotFrequency(u32 slot_index, DWORD frequency) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.last_frequency = frequency;
    g_direct_sound_state.last_result = buffer->SetFrequency(frequency);
    return g_direct_sound_state.last_result;
}

void SetCurrentSoundBufferFrequency() {
    SetDirectSoundBufferSlotFrequency(g_direct_sound_state.current_slot_index,
        g_direct_sound_state.last_frequency);
}

HRESULT OffsetDirectSoundBufferSlotVolume(u32 slot_index, LONG volume_delta) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.volume_delta = volume_delta;
    g_direct_sound_state.last_result =
        buffer->SetVolume(g_direct_sound_state.last_volume + volume_delta);
    return g_direct_sound_state.last_result;
}

void AdjustCurrentSoundBufferVolume() {
    OffsetDirectSoundBufferSlotVolume(g_direct_sound_state.current_slot_index,
        g_direct_sound_state.volume_delta);
}

HRESULT OffsetDirectSoundBufferSlotPan(u32 slot_index, LONG pan_delta) {
    SetCurrentDirectSoundBufferSlotIndex(slot_index);
    auto* buffer = direct_sound_buffer_slot(slot_index);
    if (buffer == nullptr) {
        g_direct_sound_state.last_result = direct_sound_slot_error();
        return g_direct_sound_state.last_result;
    }

    g_direct_sound_state.pan_delta = pan_delta;
    g_direct_sound_state.last_result =
        buffer->SetPan(g_direct_sound_state.last_pan + pan_delta);
    return g_direct_sound_state.last_result;
}

void AdjustCurrentSoundBufferPan() {
    OffsetDirectSoundBufferSlotPan(g_direct_sound_state.current_slot_index,
        g_direct_sound_state.pan_delta);
}

u32 AllocateDirectSoundBufferSlotIndex() {
    ensure_direct_sound_slots();
    if (g_direct_sound_state.next_allocated_slot < kMaxDirectSoundBufferSlots) {
        return g_direct_sound_state.next_allocated_slot++;
    }
    return kInvalidDirectSoundBufferSlot;
}

void ReleaseDirectSoundBufferSlotsFrom(u32 first_slot) {
    ensure_direct_sound_slots();
    while (first_slot < g_direct_sound_state.next_allocated_slot) {
        --g_direct_sound_state.next_allocated_slot;
        ReleaseDirectSoundBufferSlot(g_direct_sound_state.next_allocated_slot);
    }
}

void ReleaseAllDirectSoundBufferSlots() {
    ReleaseDirectSoundBufferSlotsFrom(0);
}

void SetNextSoundBufferStaticFlag() {
    SetNextDirectSoundBufferExtraFlags(2);
}

void SetNextDirectSoundBufferExtraFlags(DWORD extra_flags) {
    g_direct_sound_state.secondary_buffer_extra_flags = extra_flags;
}

void ClearNextSoundBufferExtraFlags() {
    ResetNextDirectSoundBufferExtraFlags();
}

void ResetNextDirectSoundBufferExtraFlags() {
    SetNextDirectSoundBufferExtraFlags(0);
}

bool UploadWaveFileToDirectSoundBuffer(LPDIRECTSOUNDBUFFER buffer, HANDLE file,
    DWORD byte_count) {
    return upload_direct_sound_buffer(buffer, byte_count,
        [file](void* target, DWORD target_bytes) {
            return read_exact_file(file, target, target_bytes);
        });
}

bool UploadOpenTrcRecordToDirectSoundBuffer(LPDIRECTSOUNDBUFFER buffer,
    TrcRecordReader& reader, DWORD byte_count) {
    return upload_direct_sound_buffer(buffer, byte_count,
        [&reader](void* target, DWORD target_bytes) {
            return ReadOpenTrcRecordBytes(reader, target, target_bytes);
        });
}

bool UploadMemoryWaveToDirectSoundBuffer(LPDIRECTSOUNDBUFFER buffer, const void* sample_data,
    DWORD byte_count) {
    const auto* cursor = static_cast<const u8*>(sample_data);
    return upload_direct_sound_buffer(buffer, byte_count,
        [&cursor](void* target, DWORD target_bytes) {
            if (target_bytes == 0) {
                return true;
            }
            if (target == nullptr || cursor == nullptr) {
                g_direct_sound_state.last_result = direct_sound_slot_error();
                return false;
            }

            std::memcpy(target, cursor, target_bytes);
            cursor += target_bytes;
            return true;
        });
}

u32 LoadWaveHandleIntoSoundBufferSlot(HANDLE file) {
    return LoadWaveHandleIntoDirectSoundBufferSlot(file);
}

u32 LoadWaveHandleIntoDirectSoundBufferSlot(HANDLE file) {
    if (!g_direct_sound_state.active || file == INVALID_HANDLE_VALUE) {
        return kInvalidDirectSoundBufferSlot;
    }

    DWORD start_position = 0;
    if (!current_file_position(file, start_position)) {
        return kInvalidDirectSoundBufferSlot;
    }

    PcmWaveInfo info{};
    if (!read_pcm_wave_header_from_file(file, info)) {
        return kInvalidDirectSoundBufferSlot;
    }

    const u32 slot = create_wave_slot_and_upload(info,
        [file](LPDIRECTSOUNDBUFFER buffer, DWORD byte_count) {
            return UploadWaveFileToDirectSoundBuffer(buffer, file, byte_count);
        });
    if (slot != kInvalidDirectSoundBufferSlot) {
        seek_file_absolute(file, start_position + info.riff_total_bytes);
    }
    return slot;
}

u32 LoadWaveFileIntoSoundBufferSlot(const char* path) {
    return LoadWaveFileIntoDirectSoundBufferSlot(path);
}

u32 LoadWaveFileIntoDirectSoundBufferSlot(const char* path) {
    if (!g_direct_sound_state.active || path == nullptr) {
        return kInvalidDirectSoundBufferSlot;
    }

    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return kInvalidDirectSoundBufferSlot;
    }

    const u32 slot = LoadWaveHandleIntoSoundBufferSlot(file);
    CloseHandle(file);
    return slot;
}

u32 LoadTrcWaveRecordIntoSoundBufferSlot(const char* archive_name, u32 record_index) {
    return LoadTrcWaveRecordIntoDirectSoundBufferSlot(archive_name, record_index);
}

u32 LoadOpenTrcWaveIntoSoundBufferSlot(TrcRecordReader& reader) {
    if (!g_direct_sound_state.active) {
        return kInvalidDirectSoundBufferSlot;
    }

    const std::size_t start_cursor = reader.cursor;
    const u32 slot = AllocateDirectSoundBufferSlotIndex();
    if (slot == kInvalidDirectSoundBufferSlot) {
        return kInvalidDirectSoundBufferSlot;
    }

    PcmWaveInfo info{};
    if (!read_trc_pcm_wave_stream_header(reader, info)) {
        ReleaseDirectSoundBufferSlotsFrom(slot);
        return kInvalidDirectSoundBufferSlot;
    }

    if (info.data_bytes <= 0x0a) {
        std::array<u8, 0x0a> discard{};
        if (info.data_bytes != 0 &&
            !ReadOpenTrcRecordBytes(reader, discard.data(), info.data_bytes)) {
            ReleaseDirectSoundBufferSlotsFrom(slot);
            return kInvalidDirectSoundBufferSlot;
        }
        if (!finish_trc_pcm_wave_stream(reader, start_cursor, info)) {
            ReleaseDirectSoundBufferSlotsFrom(slot);
            return kInvalidDirectSoundBufferSlot;
        }
        ensure_direct_sound_slots();
        g_direct_sound_state.secondary_buffers[slot] = nullptr;
        return slot;
    }

    const bool built = BuildSecondarySoundBufferSlot(slot, info.data_bytes, info.sample_rate,
        info.bits_per_sample, info.block_align, info.channels,
        g_direct_sound_state.secondary_buffer_extra_flags);
    if (!built) {
        ReleaseDirectSoundBufferSlotsFrom(slot);
        return kInvalidDirectSoundBufferSlot;
    }

    auto* buffer = direct_sound_buffer_slot(slot);
    if (buffer == nullptr || !UploadOpenTrcRecordToDirectSoundBuffer(buffer, reader,
            info.data_bytes)) {
        ReleaseDirectSoundBufferSlot(slot);
        ReleaseDirectSoundBufferSlotsFrom(slot);
        return kInvalidDirectSoundBufferSlot;
    }
    if (!finish_trc_pcm_wave_stream(reader, start_cursor, info)) {
        ReleaseDirectSoundBufferSlot(slot);
        ReleaseDirectSoundBufferSlotsFrom(slot);
        return kInvalidDirectSoundBufferSlot;
    }

    return slot;
}

u32 LoadTrcWaveRecordIntoDirectSoundBufferSlot(const char* archive_name, u32 record_index) {
    if (!g_direct_sound_state.active || archive_name == nullptr) {
        return kInvalidDirectSoundBufferSlot;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return kInvalidDirectSoundBufferSlot;
    }

    const u32 slot = LoadOpenTrcWaveIntoSoundBufferSlot(reader);
    CloseTrcRecordReader(reader);
    return slot;
}

u32 LoadMemoryWaveIntoSoundBufferSlot(const void* wave_data, u32* total_wave_bytes) {
    return LoadMemoryWaveIntoDirectSoundBufferSlot(wave_data, total_wave_bytes);
}

u32 LoadMemoryWaveIntoDirectSoundBufferSlot(const void* wave_data, u32* total_wave_bytes) {
    if (!g_direct_sound_state.active || wave_data == nullptr) {
        return kInvalidDirectSoundBufferSlot;
    }

    if (total_wave_bytes != nullptr) {
        const auto* bytes = static_cast<const u8*>(wave_data);
        *total_wave_bytes = read_le_u32(bytes + 4) + 8;
    }

    PcmWaveInfo info{};
    if (!parse_pcm_wave_memory(wave_data, info)) {
        return kInvalidDirectSoundBufferSlot;
    }

    return create_wave_slot_and_upload(info,
        [&info](LPDIRECTSOUNDBUFFER buffer, DWORD byte_count) {
            return UploadMemoryWaveToDirectSoundBuffer(buffer, info.sample_data, byte_count);
        });
}

void SetBinkVideoPlaybackCallback(BinkVideoPlaybackCallback callback, void* user_data) {
    g_bink_video_callback = callback;
    g_bink_video_callback_user_data = user_data;
}

bool PlayBinkTrcRecord(const char* archive_name, u32 record_index, i32 x, i32 y) {
    prepare_bink_video_state(archive_name, record_index, x, y);

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        g_bink_video_state.failed = true;
        return false;
    }

    std::vector<u8> payload;
    payload = std::move(reader.payload);
    const TrcDirectoryEntry entry = reader.entry;
    const std::string archive_path = reader.archive_path;
    const u32 payload_offset = reader.payload_offset;
    const bool prefer_file_handle = entry.method == 0 && entry.stored_size == entry.original_size;
    CloseTrcRecordReader(reader);

    g_bink_video_state.record_name = entry.name;
    g_bink_video_state.payload_bytes = payload.size();

    BinkFileHeaderInfo info{};
    if (!parse_bink_file_header(payload, info)) {
        g_bink_video_state.failed = true;
        return false;
    }

    apply_bink_header_to_state(info);
    update_bink_target_coordinates(x, y);

    bool ok = play_bink_payload_with_api(payload, archive_path.c_str(), payload_offset,
        prefer_file_handle);
    if (!ok && g_bink_video_callback != nullptr) {
        g_bink_video_state.active = true;
        ok = g_bink_video_callback(g_bink_video_state, payload,
            g_bink_video_callback_user_data);
        g_bink_video_state.active = false;
        g_bink_video_state.played_with_callback = ok;
    }

    g_bink_video_state.completed = ok;
    g_bink_video_state.failed = !ok;
    return ok;
}

bool PlayBinkSource(const void* source, u32 open_flags, i32 x, i32 y) {
    prepare_bink_video_state(nullptr, 0, x, y);
    if (source == nullptr) {
        g_bink_video_state.failed = true;
        return false;
    }

    BinkApi& api = bink_api();
    g_bink_video_state.bink_api_ready = api.ready();
    if (!api.ready() || !g_direct_draw_state.active || g_direct_draw_state.back_surface == nullptr) {
        g_bink_video_state.failed = true;
        return false;
    }

    api.set_sound_system(api.open_direct_sound,
        reinterpret_cast<void*>(g_direct_sound_state.direct_sound));
    g_bink_video_state.surface_type = api.dd_surface_type(g_direct_draw_state.back_surface);

    BinkMovieHeaderPrefix* handle = api.open(source, open_flags);
    if (handle == nullptr) {
        g_bink_video_state.failed = true;
        return false;
    }

    const bool ok = play_open_bink_handle_with_api(api, handle, x, y);
    api.close(handle);
    g_bink_video_state.completed = ok;
    g_bink_video_state.failed = !ok;
    return ok;
}

bool RenderBinkFrameToBackBuffer(void* bink_handle) {
    BinkApi& api = bink_api();
    g_bink_video_state.bink_api_ready = api.ready();
    if (!api.ready() || bink_handle == nullptr) {
        return false;
    }

    return render_bink_frame_with_api(api,
        static_cast<BinkMovieHeaderPrefix*>(bink_handle));
}

void CancelBinkVideoPlayback() {
    g_bink_video_state.cancelled = true;
}

bool ConfigureBinkFrameSurface() {
    if (!g_direct_draw_state.active || g_direct_draw_state.back_surface == nullptr) {
        g_bink_video_state.failed = true;
        g_bink_video_state.frame_surface_configured = false;
        return false;
    }

    BinkApi& api = bink_api();
    g_bink_video_state.bink_api_ready = api.ready();
    if (api.ready()) {
        g_bink_video_state.surface_type = api.dd_surface_type(g_direct_draw_state.back_surface);
    }
    g_bink_video_state.frame_surface_configured = true;
    return true;
}

void HandleJw208IntroVideoSequence(HWND window) {
    HandleDirectDrawFrameBoundary();

    const u32 previous_color_depth = g_direct_draw_state.color_depth;

    ConfigureDirectDrawSurfaces(window, 640, 480, static_cast<int>(previous_color_depth));
    HandleDirectDrawFrameBoundary();

    SetPrimaryMilesMusicPolicyMode(1);
    PlayBinkTrcRecord("JW2_08.TRC", 0, -1, -1);
    HandleDirectDrawFrameBoundary();
    PlayBinkTrcRecord("JW2_08.TRC", 1, -1, -1);
    HandleDirectDrawFrameBoundary();
    PlayBinkTrcRecord("JW2_08.TRC", 2, -1, -1);
    HandleBackBufferFadeToBlack();
    HandleDirectDrawFrameBoundary();

    ConfigureDirectDrawSurfaces(window, 800, 600, static_cast<int>(previous_color_depth));
    HandleDirectDrawFrameBoundary();
}

void HandleJw208Record3VideoTransition(HWND window) {
    (void)window;
    HandleDirectDrawFrameBoundary();
    SetPrimaryMilesMusicPolicyMode(1);
    PlayBinkTrcRecord("JW2_08.TRC", 3, -1, -1);
    HandleBackBufferFadeToBlack();
    HandleDirectDrawFrameBoundary();
}

const DirectDrawRuntimeState& direct_draw_state() {
    return g_direct_draw_state;
}

const DirectSoundRuntimeState& direct_sound_state() {
    return g_direct_sound_state;
}

const BinkVideoRuntimeState& bink_video_state() {
    return g_bink_video_state;
}

}
#endif
