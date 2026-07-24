#include "ranker_ui_screen.h"

#include "ranker_cursor.h"
#include "ranker_directx.h"
#include "ranker_input.h"
#include "ranker_miles.h"
#include "ranker_palette_cache.h"
#include "ranker_resource_store.h"
#include "ranker_runtime_resources.h"
#include "ranker_sprite_renderer.h"
#include "ranker_text_renderer.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"
#include "zlib.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

#ifdef _WIN32
#include <mmsystem.h>
#endif

namespace ranker {
namespace {

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

u16 read_le_u16(const u8* p) {
    return static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8);
}

void write_le_i32(u8* p, i32 value) {
    const u32 raw = static_cast<u32>(value);
    p[0] = static_cast<u8>(raw & 0xff);
    p[1] = static_cast<u8>((raw >> 8) & 0xff);
    p[2] = static_cast<u8>((raw >> 16) & 0xff);
    p[3] = static_cast<u8>((raw >> 24) & 0xff);
}

i16 entry_i16(const UiScreenEntry& entry, std::size_t offset) {
    if (offset + sizeof(u16) > entry.bytes.size()) {
        return 0;
    }
    return static_cast<i16>(read_le_u16(entry.bytes.data() + offset));
}

const char* entry_text(const UiScreenEntry& entry) {
    constexpr std::size_t kTextOffset = 0xa4;
    if (kTextOffset >= entry.bytes.size()) {
        return "";
    }
    return reinterpret_cast<const char*>(entry.bytes.data() + kTextOffset);
}

char* entry_text_mut(UiScreenEntry& entry) {
    constexpr std::size_t kTextOffset = 0xa4;
    return reinterpret_cast<char*>(entry.bytes.data() + kTextOffset);
}

std::size_t entry_text_capacity(const UiScreenEntry& entry) {
    constexpr std::size_t kTextOffset = 0xa4;
    const i32 configured = UiScreenEntryI32(entry, 0xa0);
    const std::size_t raw_capacity = entry.bytes.size() - kTextOffset;
    if (configured <= 0) {
        return raw_capacity;
    }
    return std::min<std::size_t>(static_cast<std::size_t>(configured), raw_capacity);
}

bool entry_contains_point(const UiScreenEntry& entry, i32 x, i32 y) {
    return UiScreenEntryI32(entry, 0x20) <= x && x <= UiScreenEntryI32(entry, 0x28) &&
        UiScreenEntryI32(entry, 0x24) <= y && y <= UiScreenEntryI32(entry, 0x2c);
}

void set_entry_state(UiScreenEntry& entry, i32 state) {
    SetUiScreenEntryI32(entry, 0, state);
}

u8 uppercase_ascii(u32 value) {
    value &= 0xffu;
    if ('a' <= value && value <= 'z') {
        value -= 0x20u;
    }
    return static_cast<u8>(value);
}

void append_text_cursor_marker(UiScreenEntry& entry) {
    char* text = entry_text_mut(entry);
    const std::size_t capacity = entry_text_capacity(entry);
    const std::size_t length = std::min<std::size_t>(std::strlen(text), capacity);
    if (capacity < 2 || length + 1 >= capacity) {
        return;
    }
    text[length] = '_';
    text[length + 1] = '\0';
}

void remove_text_cursor_marker(UiScreenEntry& entry) {
    char* text = entry_text_mut(entry);
    const std::size_t capacity = entry_text_capacity(entry);
    const std::size_t length = std::min<std::size_t>(std::strlen(text), capacity);
    if (length == 0) {
        return;
    }
    text[length - 1] = '\0';
}

void sleep_one_millisecond() {
#ifdef _WIN32
    Sleep(1);
#endif
}

u32 resource_width(u32 resource_index) {
    const ResourceStoreEntry* resource = GetResourceEntry(resource_index);
    return resource != nullptr ? resource->metadata[0] : 0;
}

u32 resource_height(u32 resource_index) {
    const ResourceStoreEntry* resource = GetResourceEntry(resource_index);
    return resource != nullptr ? resource->metadata[1] : 0;
}

u32 screen_resource_index(const UiScreenDefinition& screen, u32 resource_index) {
    if (resource_index == kInvalidResourceEntry) {
        return kInvalidResourceEntry;
    }
    if (screen.resource_mark == kInvalidUiScreenIndex) {
        return resource_index;
    }
    if (resource_index >= kResourceStoreCapacity - screen.resource_mark) {
        return kInvalidResourceEntry;
    }
    return screen.resource_mark + resource_index;
}

u32 screen_resource_width(const UiScreenDefinition& screen, u32 resource_index) {
    return resource_width(screen_resource_index(screen, resource_index));
}

u32 screen_resource_height(const UiScreenDefinition& screen, u32 resource_index) {
    return resource_height(screen_resource_index(screen, resource_index));
}

void clamp_scroll_value(UiScreenEntry& entry) {
    const i32 max_value = UiScreenEntryI32(entry, 0x50);
    i32 value = UiScreenEntryI32(entry, 0x54);
    if (max_value <= 0) {
        SetUiScreenEntryI32(entry, 0x54, 0);
        return;
    }
    value = std::clamp(value, 0, max_value - 1);
    SetUiScreenEntryI32(entry, 0x54, value);
}

void update_active_scroll_tracking(UiScreenDefinition& screen, i32 mouse_x, i32 mouse_y) {
    if (!screen.scroll_tracking || screen.active_scroll_entry >= screen.entries.size()) {
        return;
    }

    UiScreenEntry& entry = screen.entries[screen.active_scroll_entry];
    const bool vertical = (static_cast<u32>(UiScreenEntryI32(entry, 0x04)) & 0x200u) != 0;
    const i32 left = UiScreenEntryI32(entry, 0x20);
    const i32 top = UiScreenEntryI32(entry, 0x24);
    const i32 right = UiScreenEntryI32(entry, 0x28);
    const i32 bottom = UiScreenEntryI32(entry, 0x2c);
    const i32 max_value = UiScreenEntryI32(entry, 0x50);
    const u32 first_button = static_cast<u32>(UiScreenEntryI32(entry, 0x60));
    const u32 second_button = static_cast<u32>(UiScreenEntryI32(entry, 0x68));
    const u32 thumb = static_cast<u32>(UiScreenEntryI32(entry, 0x70));

    if (!entry_contains_point(entry, mouse_x, mouse_y)) {
        SetUiScreenEntryI32(entry, 0x58, 0);
        return;
    }

    const u32 pressed_region = screen.scroll_flags & 0x7u;
    SetUiScreenEntryI32(entry, 0x58, UiScreenEntryI32(entry, 0x58) |
        static_cast<i32>(pressed_region));

    const i32 track_start = vertical ?
        top + static_cast<i32>(screen_resource_height(screen, first_button)) :
        left + static_cast<i32>(screen_resource_width(screen, first_button));
    const i32 track_end = vertical ?
        bottom - static_cast<i32>(screen_resource_height(screen, second_button)) :
        right - static_cast<i32>(screen_resource_width(screen, second_button));
    const i32 thumb_size = vertical ? static_cast<i32>(screen_resource_height(screen, thumb)) :
        static_cast<i32>(screen_resource_width(screen, thumb));

    if (max_value <= 0 || track_end <= track_start) {
        SetUiScreenEntryI32(entry, 0x54, 0);
        return;
    }

#ifdef _WIN32
    const u32 now = timeGetTime();
#else
    const u32 now = screen.last_scroll_tick + 1;
#endif
    const u32 repeat_interval = static_cast<u32>(std::max(1, track_end - track_start)) /
        static_cast<u32>(max_value);

    if ((pressed_region & 1u) != 0) {
        if (screen.last_scroll_tick + repeat_interval < now) {
            SetUiScreenEntryI32(entry, 0x54, UiScreenEntryI32(entry, 0x54) - 1);
            screen.last_scroll_tick = now;
        }
    }
    else if ((pressed_region & 2u) != 0) {
        if (screen.last_scroll_tick + repeat_interval < now) {
            SetUiScreenEntryI32(entry, 0x54, UiScreenEntryI32(entry, 0x54) + 1);
            screen.last_scroll_tick = now;
        }
    }
    else if ((pressed_region & 4u) != 0) {
        const i32 travel = (track_end - track_start) - thumb_size;
        if (travel > 0) {
            const i32 axis = vertical ? mouse_y : mouse_x;
            SetUiScreenEntryI32(entry, 0x54, ((axis - track_start) * max_value) / travel);
        }
    }

    clamp_scroll_value(entry);
}

i32 selected_state_sprite(const UiScreenEntry& entry) {
    switch (UiScreenEntryI32(entry, 0) + 1) {
    case 0:
        return UiScreenEntryI32(entry, 0x3c);
    case 1:
        return UiScreenEntryI32(entry, 0x30);
    case 2:
        return UiScreenEntryI32(entry, 0x34);
    case 3:
        return UiScreenEntryI32(entry, 0x38);
    default:
        return -1;
    }
}

u8 selected_text_color(const UiScreenEntry& entry) {
    switch (UiScreenEntryI32(entry, 0) + 1) {
    case 0:
        return static_cast<u8>(UiScreenEntryI32(entry, 0x80));
    case 1:
        return static_cast<u8>(UiScreenEntryI32(entry, 0x74));
    case 2:
        return static_cast<u8>(UiScreenEntryI32(entry, 0x78));
    case 3:
    case 4:
        return static_cast<u8>(UiScreenEntryI32(entry, 0x7c));
    default:
        return 1;
    }
}

#ifdef _WIN32
constexpr u32 kBinkOpenFromMemory = 0x04000000u;
constexpr u32 kBinkCopyDirectDrawFlags = 0x80080000u;

struct BinkHeaderPrefix {
    u32 width = 0;
    u32 height = 0;
    u32 frame_count = 0;
    u32 frame_number = 0;
};

struct BinkApi {
    using OpenDirectSoundFn = void* (WINAPI*)(void*);
    using SetSoundSystemFn = i32 (WINAPI*)(OpenDirectSoundFn, void*);
    using DdSurfaceTypeFn = u32 (WINAPI*)(void*);
    using OpenFn = BinkHeaderPrefix* (WINAPI*)(const void*, u32);
    using CloseFn = void (WINAPI*)(BinkHeaderPrefix*);
    using WaitFn = i32 (WINAPI*)(BinkHeaderPrefix*);
    using DoFrameFn = void (WINAPI*)(BinkHeaderPrefix*);
    using NextFrameFn = void (WINAPI*)(BinkHeaderPrefix*);
    using PauseFn = void (WINAPI*)(BinkHeaderPrefix*, i32);
    using CopyToBufferFn = void (WINAPI*)(BinkHeaderPrefix*, void*, i32, u32, i32, i32, u32);

    HMODULE module = nullptr;
    OpenDirectSoundFn open_direct_sound = nullptr;
    SetSoundSystemFn set_sound_system = nullptr;
    DdSurfaceTypeFn dd_surface_type = nullptr;
    OpenFn open = nullptr;
    CloseFn close = nullptr;
    WaitFn wait = nullptr;
    DoFrameFn do_frame = nullptr;
    NextFrameFn next_frame = nullptr;
    PauseFn pause = nullptr;
    CopyToBufferFn copy_to_buffer = nullptr;

    bool ready() const {
        return module != nullptr && open_direct_sound != nullptr && set_sound_system != nullptr &&
            dd_surface_type != nullptr && open != nullptr && close != nullptr &&
            wait != nullptr && do_frame != nullptr && next_frame != nullptr &&
            pause != nullptr && copy_to_buffer != nullptr;
    }
};

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
    api.pause = load_bink_proc<BinkApi::PauseFn>(api.module, { "_BinkPause@8", "BinkPause" });
    api.copy_to_buffer = load_bink_proc<BinkApi::CopyToBufferFn>(
        api.module, { "_BinkCopyToBuffer@28", "BinkCopyToBuffer" });

    if (!api.ready()) {
        FreeLibrary(api.module);
        api = BinkApi{};
    }
    return api;
}

void close_bink_entry(UiScreenBinkEntryState& state) {
    if (state.handle != nullptr) {
        BinkApi& api = bink_api();
        if (api.close != nullptr) {
            api.close(static_cast<BinkHeaderPrefix*>(state.handle));
        }
    }
    state.handle = nullptr;
    state.source = nullptr;
    state.paused = false;
}

bool initialize_bink_runtime(UiScreenDefinition& screen) {
    if (screen.bink_initialized) {
        return true;
    }

    const auto& dd = direct_draw_state();
    if (!dd.active || dd.back_surface == nullptr) {
        return false;
    }

    BinkApi& api = bink_api();
    if (!api.ready()) {
        return false;
    }

    api.set_sound_system(api.open_direct_sound,
        reinterpret_cast<void*>(direct_sound_state().direct_sound));
    screen.bink_surface_type = api.dd_surface_type(dd.back_surface);
    screen.bink_initialized = true;
    return true;
}

const void* bink_blob_for_entry(const UiScreenDefinition& screen, const UiScreenEntry& entry) {
    const i32 blob_index = UiScreenEntryI32(entry, 0x4c);
    if (blob_index < 0 || static_cast<u32>(blob_index) >= screen.embedded_blob_count ||
        static_cast<std::size_t>(blob_index) >= screen.embedded_blobs.size()) {
        return nullptr;
    }

    const auto& blob = screen.embedded_blobs[static_cast<std::size_t>(blob_index)];
    return blob.empty() ? nullptr : blob.data();
}

bool open_bink_entry(UiScreenDefinition& screen, const UiScreenEntry& entry,
    UiScreenBinkEntryState& state) {
    BinkApi& api = bink_api();
    if (!api.ready()) {
        return false;
    }

    const void* source = bink_blob_for_entry(screen, entry);
    if (source == nullptr) {
        return false;
    }

    state.source = source;
    state.handle = api.open(source, kBinkOpenFromMemory);
    state.paused = false;
    if (state.handle == nullptr) {
        state.source = nullptr;
        return false;
    }
    return true;
}

std::string module_directory() {
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(sizeof(path)));
    if (length == 0 || length >= sizeof(path)) {
        return {};
    }

    std::string result(path, length);
    const std::size_t slash = result.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    result.resize(slash);
    return result;
}

bool read_binary_file(const char* path, std::vector<u8>& out) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }

    out.assign(static_cast<std::size_t>(size), 0);
    const bool ok = out.empty() ||
        std::fread(out.data(), 1, out.size(), file) == out.size();
    std::fclose(file);
    if (!ok) {
        out.clear();
    }
    return ok;
}

#ifdef _WIN32
bool read_embedded_binary_resource(WORD resource_id, std::vector<u8>& out) {
    HMODULE module = GetModuleHandleA(nullptr);
    HRSRC resource = FindResourceA(module,
        MAKEINTRESOURCEA(resource_id), RT_RCDATA);
    if (resource == nullptr) {
        return false;
    }

    HGLOBAL loaded = LoadResource(module, resource);
    const DWORD byte_count = SizeofResource(module, resource);
    const void* data = loaded == nullptr ? nullptr : LockResource(loaded);
    if (data == nullptr || byte_count == 0) {
        return false;
    }

    const auto* first = static_cast<const u8*>(data);
    out.assign(first, first + byte_count);
    return true;
}
#endif

const std::vector<u8>& main_menu_bink_fallback_565() {
    static std::vector<u8> bytes;
    static bool attempted = false;
    if (attempted) {
        return bytes;
    }

    attempted = true;
#ifdef _WIN32
    constexpr WORD kMainMenuBinkFallbackResourceId = 2000;
    if (read_embedded_binary_resource(kMainMenuBinkFallbackResourceId, bytes)) {
        return bytes;
    }
#endif

    const std::string module_dir = module_directory();
    std::array<std::string, 5> candidates{
        "main_menu_bink_fallback_565.bin",
        "resources\\main_menu_bink_fallback_565.bin",
        module_dir.empty() ? std::string{} :
            module_dir + "\\main_menu_bink_fallback_565.bin",
        module_dir.empty() ? std::string{} :
            module_dir + "\\..\\resources\\main_menu_bink_fallback_565.bin",
        module_dir.empty() ? std::string{} :
            module_dir + "\\resources\\main_menu_bink_fallback_565.bin",
    };

    for (const std::string& candidate : candidates) {
        if (!candidate.empty() && read_binary_file(candidate.c_str(), bytes)) {
            break;
        }
    }
    return bytes;
}

const std::vector<u8>& main_menu_bink_fallback_animation_565() {
    constexpr std::size_t kFrameBytes = 750u * 269u * sizeof(u16);
    constexpr std::size_t kFrameCount = 60u;
    constexpr std::size_t kAnimationBytes = kFrameBytes * kFrameCount;

    static std::vector<u8> frames;
    static bool attempted = false;
    if (attempted) {
        return frames;
    }
    attempted = true;

    std::vector<u8> packed;
#ifdef _WIN32
    constexpr WORD kMainMenuBinkAnimationResourceId = 2001;
    (void)read_embedded_binary_resource(kMainMenuBinkAnimationResourceId, packed);
#endif
    if (packed.empty()) {
        const std::string module_dir = module_directory();
        std::array<std::string, 5> candidates{
            "main_menu_bink_fallback_anim.z",
            "resources\\main_menu_bink_fallback_anim.z",
            module_dir.empty() ? std::string{} :
                module_dir + "\\main_menu_bink_fallback_anim.z",
            module_dir.empty() ? std::string{} :
                module_dir + "\\..\\resources\\main_menu_bink_fallback_anim.z",
            module_dir.empty() ? std::string{} :
                module_dir + "\\resources\\main_menu_bink_fallback_anim.z",
        };
        for (const std::string& candidate : candidates) {
            if (!candidate.empty() && read_binary_file(candidate.c_str(), packed)) {
                break;
            }
        }
    }
    if (packed.empty()) {
        return frames;
    }

    frames.resize(kAnimationBytes);
    uLongf decoded_size = static_cast<uLongf>(frames.size());
    if (uncompress(frames.data(), &decoded_size, packed.data(),
            static_cast<uLong>(packed.size())) != Z_OK ||
        decoded_size != frames.size()) {
        frames.clear();
        return frames;
    }

    // The resource stores frame zero followed by XOR deltas.  Rebuild all
    // frames once at load time so title redraws remain a cheap indexed copy.
    for (std::size_t frame = 1; frame < kFrameCount; ++frame) {
        const std::size_t current = frame * kFrameBytes;
        const std::size_t previous = current - kFrameBytes;
        for (std::size_t byte = 0; byte < kFrameBytes; ++byte) {
            frames[current + byte] ^= frames[previous + byte];
        }
    }
    return frames;
}

u16 raw_565_to_target_pixel(u16 pixel) {
    if (!SurfacePixelMode555()) {
        return pixel;
    }
    const u16 red = static_cast<u16>((pixel >> 11) & 0x1fu);
    const u16 green = static_cast<u16>((pixel >> 6) & 0x1fu);
    const u16 blue = static_cast<u16>(pixel & 0x1fu);
    return static_cast<u16>((red << 10) | (green << 5) | blue);
}

bool bink_fallback_target_valid(const SpriteRenderTarget& target) {
    return target.pixels != nullptr && target.width != 0 && target.height != 0 &&
        target.stride_words != 0;
}

bool draw_main_menu_bink_fallback_to_target(const SpriteRenderTarget& target,
    const UiScreenEntry& entry, u32 width, u32 height, const std::vector<u8>& bytes,
    std::size_t source_offset = 0) {
    const std::size_t frame_bytes =
        static_cast<std::size_t>(width) * height * sizeof(u16);
    if (!bink_fallback_target_valid(target) ||
        source_offset > bytes.size() || frame_bytes > bytes.size() - source_offset) {
        return false;
    }

    const i32 draw_x = UiScreenEntryI32(entry, 0x20);
    const i32 draw_y = UiScreenEntryI32(entry, 0x24);
    std::size_t source = source_offset;
    for (u32 row = 0; row < height; ++row) {
        const i32 target_y = draw_y + static_cast<i32>(row);
        for (u32 col = 0; col < width; ++col) {
            const u16 pixel = raw_565_to_target_pixel(read_le_u16(bytes.data() + source));
            source += sizeof(u16);

            const i32 target_x = draw_x + static_cast<i32>(col);
            if (target_x >= 0 && target_x < static_cast<i32>(target.width) &&
                target_y >= 0 && target_y < static_cast<i32>(target.height)) {
                target.pixels[static_cast<std::size_t>(target_y) * target.stride_words +
                    static_cast<std::size_t>(target_x)] = pixel;
            }
        }
    }
    return true;
}

bool draw_main_menu_bink_fallback(UiScreenDefinition& screen, const UiScreenEntry& entry,
    UiScreenBinkEntryState& state) {
    const auto* source = static_cast<const u8*>(bink_blob_for_entry(screen, entry));
    if (source == nullptr || std::memcmp(source, "BIKi", 4) != 0) {
        return false;
    }

    const u32 width = read_le_u32(source + 0x14);
    const u32 height = read_le_u32(source + 0x18);
    if (width != 750 || height != 269) {
        return false;
    }

    constexpr u64 kFrameRate = 15u;
    constexpr std::size_t kFrameCount = 60u;
    const std::size_t frame_bytes =
        static_cast<std::size_t>(width) * height * sizeof(u16);
    const std::vector<u8>& animation = main_menu_bink_fallback_animation_565();
    const std::vector<u8>* bytes = &animation;
    std::size_t source_offset = 0;
    if (animation.size() >= frame_bytes * kFrameCount) {
        const u64 now = GetTickCount64();
        if (state.fallback_started_tick_ms == 0) {
            state.fallback_started_tick_ms = now;
        }
        const std::size_t frame = static_cast<std::size_t>(
            (((now - state.fallback_started_tick_ms) * kFrameRate) / 1000u) %
            kFrameCount);
        source_offset = frame * frame_bytes;
    }
    else {
        bytes = &main_menu_bink_fallback_565();
    }
    const SpriteRenderTarget& active_target = sprite_render_state().target;
    if (bink_fallback_target_valid(active_target)) {
        return draw_main_menu_bink_fallback_to_target(
            active_target, entry, width, height, *bytes, source_offset);
    }

    SpriteRenderTarget target{};
    if (FAILED(LockBackBufferSpriteRenderTarget(target))) {
        return false;
    }
    const bool ok = draw_main_menu_bink_fallback_to_target(
        target, entry, width, height, *bytes, source_offset);
    UnlockBackBufferSpriteRenderTarget();
    return ok;
}
#endif

bool ui_render_target_valid(const SpriteRenderTarget& target) {
    return target.pixels != nullptr && target.width != 0 && target.height != 0 &&
        target.stride_words != 0;
}

template <typename Draw>
bool draw_with_backbuffer_target(const Draw& draw) {
    const SpriteRenderTarget& target = sprite_render_state().target;
    if (ui_render_target_valid(target)) {
        return draw(target);
    }

#ifdef _WIN32
    SpriteRenderTarget locked_target{};
    if (FAILED(LockBackBufferSpriteRenderTarget(locked_target))) {
        return false;
    }
    const bool ok = draw(locked_target);
    UnlockBackBufferSpriteRenderTarget();
    return ok;
#else
    return false;
#endif
}

void put_ui_pixel(const SpriteRenderTarget& target, i32 x, i32 y, u16 color) {
    if (x < 0 || y < 0 || x >= static_cast<i32>(target.width) ||
        y >= static_cast<i32>(target.height)) {
        return;
    }
    target.pixels[static_cast<std::size_t>(y) * target.stride_words +
        static_cast<std::size_t>(x)] = color;
}

u16 darken_ui_pixel(u16 pixel) {
    const bool pixel_mode_555 = SurfacePixelMode555();
    if (pixel_mode_555) {
        const u16 red = static_cast<u16>((pixel >> 10) & 0x1fu);
        const u16 green = static_cast<u16>((pixel >> 5) & 0x1fu);
        const u16 blue = static_cast<u16>(pixel & 0x1fu);
        const u16 gray = static_cast<u16>((red + green + blue) >> 2);
        return static_cast<u16>((gray << 10) | (gray << 5) | gray);
    }

    const u16 red = static_cast<u16>((pixel >> 11) & 0x1fu);
    const u16 green = static_cast<u16>((pixel >> 5) & 0x3fu);
    const u16 blue = static_cast<u16>(pixel & 0x1fu);
    const u16 gray = static_cast<u16>((red + (green >> 1) + blue) >> 2);
    return static_cast<u16>((gray << 11) | ((gray << 6) & 0x07e0u) | gray);
}

bool draw_rectangle_outline_to_target(const SpriteRenderTarget& target,
    i32 left, i32 top, i32 width, i32 height, u16 color) {
    if (!ui_render_target_valid(target)) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return true;
    }

    const i32 right = left + width - 1;
    const i32 bottom = top + height - 1;
    for (i32 x = left; x < left + width; ++x) {
        put_ui_pixel(target, x, top, color);
        put_ui_pixel(target, x, bottom, color);
    }
    for (i32 y = top; y < top + height; ++y) {
        put_ui_pixel(target, left, y, color);
        put_ui_pixel(target, right, y, color);
    }
    return true;
}

bool darken_rectangle_to_target(const SpriteRenderTarget& target,
    i32 left, i32 top, i32 right, i32 bottom) {
    if (!ui_render_target_valid(target)) {
        return false;
    }
    if (right < left || bottom < top) {
        return true;
    }

    for (i32 y = top; y <= bottom; ++y) {
        if (y < 0 || y >= static_cast<i32>(target.height)) {
            continue;
        }
        for (i32 x = left; x <= right; ++x) {
            if (x < 0 || x >= static_cast<i32>(target.width)) {
                continue;
            }
            u16& pixel = target.pixels[static_cast<std::size_t>(y) * target.stride_words +
                static_cast<std::size_t>(x)];
            pixel = darken_ui_pixel(pixel);
        }
    }
    return true;
}

bool draw_line_to_target(const SpriteRenderTarget& target,
    i32 x0, i32 y0, i32 x1, i32 y1, u16 color) {
    if (!ui_render_target_valid(target)) {
        return false;
    }

    i32 step_x = -1;
    i32 dx = x0 - x1;
    if (dx < 0) {
        dx = -dx;
        step_x = 1;
    }

    i32 step_y = -1;
    i32 dy = y0 - y1;
    if (dy < 0) {
        dy = -dy;
        step_y = 1;
    }

    i32 accum_x = dx;
    i32 accum_y = dy;
    if (dx < dy) {
        while (x0 != x1 || y0 != y1) {
            put_ui_pixel(target, x0, y0, color);
            y0 += step_y;
            accum_x += dx;
            if (accum_y <= accum_x) {
                accum_x -= accum_y;
                x0 += step_x;
            }
        }
    } else {
        while (x0 != x1 || y0 != y1) {
            put_ui_pixel(target, x0, y0, color);
            x0 += step_x;
            accum_y += dy;
            if (accum_x <= accum_y) {
                accum_y -= accum_x;
                y0 += step_y;
            }
        }
    }
    return true;
}

bool or_mask_32x32_to_target(const SpriteRenderTarget& target, i32 left, i32 top,
    u16 mask) {
    if (!ui_render_target_valid(target)) {
        return false;
    }

    for (i32 y = top; y < top + 32; ++y) {
        if (y < 0 || y >= static_cast<i32>(target.height)) {
            continue;
        }
        for (i32 x = left; x < left + 32; ++x) {
            if (x < 0 || x >= static_cast<i32>(target.width)) {
                continue;
            }
            u16& pixel = target.pixels[static_cast<std::size_t>(y) * target.stride_words +
                static_cast<std::size_t>(x)];
            pixel = static_cast<u16>(pixel | mask);
        }
    }
    return true;
}

bool draw_filled_rectangle_to_target(const SpriteRenderTarget& target,
    i32 left, i32 top, i32 right, i32 bottom, u16 color, bool stippled) {
    if (!ui_render_target_valid(target)) {
        return false;
    }
    if (right < left || bottom < top) {
        return true;
    }

    for (i32 y = top; y <= bottom; ++y) {
        u32 stipple_counter = 1;
        for (i32 x = left; x <= right; ++x) {
            if (!stippled || (stipple_counter & 3u) != 0) {
                put_ui_pixel(target, x, y, color);
            }
            ++stipple_counter;
        }
    }
    return true;
}

struct UiScreenOperation {
    u32 type = 0;
    u32 payload_size = 0;
};

class MemoryReader {
public:
    MemoryReader(const u8* data, std::size_t size) : cursor_(data), remaining_(size) {}

    bool read(void* out, std::size_t size) {
        if (size > remaining_ || (out == nullptr && size != 0)) {
            return false;
        }
        if (size != 0) {
            std::memcpy(out, cursor_, size);
            cursor_ += size;
            remaining_ -= size;
        }
        return true;
    }

    bool skip(std::size_t size) {
        if (size > remaining_) {
            return false;
        }
        cursor_ += size;
        remaining_ -= size;
        return true;
    }

    const u8* current() const {
        return cursor_;
    }

    std::size_t remaining() const {
        return remaining_;
    }

private:
    const u8* cursor_ = nullptr;
    std::size_t remaining_ = 0;
};

#ifdef _WIN32
bool read_exact_file(HANDLE file, void* out, DWORD byte_count) {
    if (byte_count == 0) {
        return true;
    }
    DWORD bytes_read = 0;
    return ReadFile(file, out, byte_count, &bytes_read, nullptr) != FALSE &&
        bytes_read == byte_count;
}

bool read_file_operation(HANDLE file, UiScreenOperation& operation) {
    std::array<u8, 8> bytes{};
    if (!read_exact_file(file, bytes.data(), static_cast<DWORD>(bytes.size()))) {
        return false;
    }
    operation.type = read_le_u32(bytes.data());
    operation.payload_size = read_le_u32(bytes.data() + 4);
    return true;
}
#endif

bool read_memory_operation(MemoryReader& reader, UiScreenOperation& operation) {
    std::array<u8, 8> bytes{};
    if (!reader.read(bytes.data(), bytes.size())) {
        return false;
    }
    operation.type = read_le_u32(bytes.data());
    operation.payload_size = read_le_u32(bytes.data() + 4);
    return true;
}

void capture_resource_marks(UiScreenDefinition& screen) {
    screen.palette_mark = palette_cache_state().next_slot;
    screen.resource_mark = resource_store_state().next_entry;
#ifdef _WIN32
    screen.sound_mark = direct_sound_state().next_allocated_slot;
#else
    screen.sound_mark = kInvalidUiScreenIndex;
#endif
}

bool allocate_entries(UiScreenDefinition& screen, u32 operation_count, u32 entry_count) {
    screen.operation_count = operation_count;
    screen.entry_count = entry_count;
    screen.entries.assign(entry_count, UiScreenEntry{});
    screen.bink_entries.assign(entry_count, UiScreenBinkEntryState{});
    return screen.entries.size() == entry_count && screen.bink_entries.size() == entry_count;
}

bool append_blob(UiScreenDefinition& screen, const void* data, u32 byte_count) {
    if (screen.embedded_blob_count >= kUiScreenBlobSlots ||
        (data == nullptr && byte_count != 0)) {
        return false;
    }

    const u32 slot = screen.embedded_blob_count;
    auto& blob = screen.embedded_blobs[slot];
    blob.assign(byte_count, 0);
    if (byte_count != 0) {
        std::memcpy(blob.data(), data, byte_count);
    }
    screen.embedded_blob_sizes[slot] = byte_count;
    ++screen.embedded_blob_count;
    return true;
}

bool load_palette_from_memory(MemoryReader& reader, u32& active_palette) {
    std::array<u8, kPaletteRawBytesPerSlot> raw{};
    if (!reader.read(raw.data(), raw.size())) {
        return false;
    }
    const u32 slot = AllocatePaletteCacheSlot();
    if (slot == kInvalidPaletteCacheSlot || !SetPaletteCacheRawSlot(slot, raw.data(), raw.size())) {
        return false;
    }
    ConvertPaletteCacheSlot(slot);
    active_palette = slot;
    return true;
}

#ifdef _WIN32
bool load_palette_from_file(HANDLE file, u32& active_palette) {
    std::array<u8, kPaletteRawBytesPerSlot> raw{};
    if (!read_exact_file(file, raw.data(), static_cast<DWORD>(raw.size()))) {
        return false;
    }
    const u32 slot = AllocatePaletteCacheSlot();
    if (slot == kInvalidPaletteCacheSlot || !SetPaletteCacheRawSlot(slot, raw.data(), raw.size())) {
        return false;
    }
    ConvertPaletteCacheSlot(slot);
    active_palette = slot;
    return true;
}

bool process_file_operation(UiScreenDefinition& screen, HANDLE file,
    const UiScreenOperation& operation, u32& active_palette) {
    switch (operation.type) {
    case 1:
        return load_palette_from_file(file, active_palette);
    case 2: {
        const u32 resource = LoadResourceHandle(file);
        return resource != kInvalidResourceEntry &&
            SetResourceEntryPaletteSlot(resource, active_palette);
    }
    case 3: {
        SetNextSoundBufferStaticFlag();
        const u32 slot = LoadWaveHandleIntoSoundBufferSlot(file);
        return slot != kInvalidUiScreenIndex;
    }
    case 4:
        return HandleUiScreenEmbeddedBlobRead(screen, file, operation.payload_size);
    default:
        return true;
    }
}
#endif

bool process_memory_operation(UiScreenDefinition& screen, MemoryReader& reader,
    const UiScreenOperation& operation, u32& active_palette) {
    switch (operation.type) {
    case 1:
        return load_palette_from_memory(reader, active_palette);
    case 2: {
        std::size_t consumed = 0;
        const u32 resource = LoadResourceMemory(reader.current(), reader.remaining(), &consumed);
        if (resource == kInvalidResourceEntry ||
            !SetResourceEntryPaletteSlot(resource, active_palette)) {
            return false;
        }
        return reader.skip(consumed);
    }
    case 3: {
#ifdef _WIN32
        SetNextSoundBufferStaticFlag();
        u32 consumed = 0;
        const u32 slot = LoadMemoryWaveIntoSoundBufferSlot(reader.current(), &consumed);
        return slot != kInvalidUiScreenIndex && consumed != 0 && reader.skip(consumed);
#else
        return false;
#endif
    }
    case 4: {
        if (operation.payload_size > reader.remaining()) {
            return false;
        }
        if (!append_blob(screen, reader.current(), operation.payload_size)) {
            return false;
        }
        return reader.skip(operation.payload_size);
    }
    default:
        return true;
    }
}

GlobalUiScreenSlotsState g_global_ui_screen_slots;

bool valid_global_ui_screen_slot(std::size_t slot) {
    return slot < kGlobalUiScreenSlotCount;
}

void release_global_ui_screen_slot(std::size_t slot) {
    if (!valid_global_ui_screen_slot(slot)) {
        return;
    }
    HandleUiScreenDefinitionReleaseWrapper(g_global_ui_screen_slots.screens[slot]);
}

void shutdown_global_ui_screen_slot_00() { release_global_ui_screen_slot(0); }
void shutdown_global_ui_screen_slot_01() { release_global_ui_screen_slot(1); }
void shutdown_global_ui_screen_slot_02() { release_global_ui_screen_slot(2); }
void shutdown_global_ui_screen_slot_03() { release_global_ui_screen_slot(3); }
void shutdown_global_ui_screen_slot_04() { release_global_ui_screen_slot(4); }
void shutdown_global_ui_screen_slot_05() { release_global_ui_screen_slot(5); }
void shutdown_global_ui_screen_slot_06() { release_global_ui_screen_slot(6); }
void shutdown_global_ui_screen_slot_07() { release_global_ui_screen_slot(7); }
void shutdown_global_ui_screen_slot_08() { release_global_ui_screen_slot(8); }
void shutdown_global_ui_screen_slot_09() { release_global_ui_screen_slot(9); }
void shutdown_global_ui_screen_slot_10() { release_global_ui_screen_slot(10); }
void shutdown_global_ui_screen_slot_11() { release_global_ui_screen_slot(11); }
void shutdown_global_ui_screen_slot_12() { release_global_ui_screen_slot(12); }
void shutdown_global_ui_screen_slot_13() { release_global_ui_screen_slot(13); }
void shutdown_global_ui_screen_slot_14() { release_global_ui_screen_slot(14); }

constexpr std::array<void (*)(), kGlobalUiScreenSlotCount> kGlobalUiScreenSlotShutdowns = {
    shutdown_global_ui_screen_slot_00,
    shutdown_global_ui_screen_slot_01,
    shutdown_global_ui_screen_slot_02,
    shutdown_global_ui_screen_slot_03,
    shutdown_global_ui_screen_slot_04,
    shutdown_global_ui_screen_slot_05,
    shutdown_global_ui_screen_slot_06,
    shutdown_global_ui_screen_slot_07,
    shutdown_global_ui_screen_slot_08,
    shutdown_global_ui_screen_slot_09,
    shutdown_global_ui_screen_slot_10,
    shutdown_global_ui_screen_slot_11,
    shutdown_global_ui_screen_slot_12,
    shutdown_global_ui_screen_slot_13,
    shutdown_global_ui_screen_slot_14,
};

GameplayModalUiState g_gameplay_modal_ui_state;

constexpr const char* kGameplayModalUiArchive = "JW2_02.TRC";
constexpr std::size_t kGameplayModalMainSlot = 0;
constexpr std::size_t kGameplayModalScenarioMessageSlot = 1;
constexpr std::size_t kGameplayModalExitSurrenderSlot = 2;
constexpr std::size_t kGameplayModalOptionsSlot = 3;
constexpr std::size_t kGameplayModalRelationSlot = 4;
constexpr std::size_t kGameplayModalObserverSlot = 5;
constexpr std::size_t kGameplayModalWaitSlot = 6;
constexpr std::size_t kGameplayModalLocalAddressSlot = 7;
constexpr std::size_t kGameplayModalStageAvailabilitySlot = 8;
constexpr std::size_t kGameplayModalSkirmishLoadSlot = 9;
constexpr std::size_t kGameplayModalSimpleInfoSlot = 10;
constexpr std::size_t kGameplayModalIndexedSelectionSlot = 11;
constexpr std::size_t kGameplayModalGameplayLoadSlot = 12;
constexpr std::size_t kGameplayModalGameplaySaveSlot = 13;

enum GameplayModalFlagIndex : std::size_t {
    kGameplayModalMainFlag = 0,
    kGameplayModalScenarioMessageFlag = 1,
    kGameplayModalExitSurrenderFlag = 2,
    kGameplayModalOptionsFlag = 3,
    kGameplayModalRelationFlag = 4,
    kGameplayModalObserverFlag = 5,
    kGameplayModalWaitFlag = 6,
};

void log_gameplay_modal_action(GameplayModalUiState& state, GameplayModalUiActionKind kind,
    u32 value0 = 0, u32 value1 = 0, const std::string& text = {}) {
    state.action_log.push_back(GameplayModalUiAction{ kind, value0, value1, text });
}

u32 selected_faction_record(const GameplayModalUiState& state, u32 base_record) {
    return state.selected_faction_id * 10 + base_record;
}

void set_gameplay_modal_flag(GameplayModalUiState& state, std::size_t flag, bool active) {
    switch (flag) {
    case kGameplayModalMainFlag:
        state.main_menu_active = active;
        break;
    case kGameplayModalScenarioMessageFlag:
        state.scenario_message_active = active;
        break;
    case kGameplayModalExitSurrenderFlag:
        state.exit_surrender_active = active;
        break;
    case kGameplayModalOptionsFlag:
        state.options_active = active;
        break;
    case kGameplayModalRelationFlag:
        state.relation_mask_active = active;
        break;
    case kGameplayModalObserverFlag:
        state.observer_mask_active = active;
        break;
    case kGameplayModalWaitFlag:
        state.wait_dialog_active = active;
        break;
    default:
        break;
    }
    SetGlobalUiScreenModalResourceActive(flag, active);
}

UiScreenDefinition& modal_screen(std::size_t slot) {
    return GlobalUiScreenSlot(slot);
}

void set_entry_text_safe(UiScreenDefinition& screen, u32 entry_index, const std::string& text) {
    if (entry_index >= screen.entries.size()) {
        return;
    }
    UiScreenEntry& entry = screen.entries[entry_index];
    char* out = entry_text_mut(entry);
    const std::size_t capacity = entry_text_capacity(entry);
    if (capacity == 0) {
        return;
    }
    const std::size_t copy_bytes = std::min<std::size_t>(capacity - 1, text.size());
    std::memcpy(out, text.data(), copy_bytes);
    out[copy_bytes] = '\0';
}

void set_entry_enabled(UiScreenDefinition& screen, u32 entry_index, bool enabled) {
    if (entry_index >= screen.entries.size()) {
        return;
    }
    UiScreenEntry& entry = screen.entries[entry_index];
    SetUiScreenEntryI32(entry, 0, enabled ? 0 : -1);
    if (!enabled) {
        SetUiScreenEntryI32(entry, 4, 0);
    }
}

void set_entry_flags_bit(UiScreenDefinition& screen, u32 entry_index, u32 bit, bool enabled) {
    if (entry_index >= screen.entries.size()) {
        return;
    }
    UiScreenEntry& entry = screen.entries[entry_index];
    u32 flags = static_cast<u32>(UiScreenEntryI32(entry, 4));
    flags = enabled ? (flags | bit) : (flags & ~bit);
    SetUiScreenEntryI32(entry, 4, static_cast<i32>(flags));
}

void set_entry_button_triplet(UiScreenDefinition& screen, u32 entry_index, i32 off_sprite,
    i32 on_sprite) {
    if (entry_index >= screen.entries.size()) {
        return;
    }
    UiScreenEntry& entry = screen.entries[entry_index];
    SetUiScreenEntryI32(entry, 0x30, off_sprite);
    SetUiScreenEntryI32(entry, 0x34, on_sprite);
    SetUiScreenEntryI32(entry, 0x38, off_sprite);
}

void set_entry_scroll_value(UiScreenDefinition& screen, u32 entry_index, i32 value) {
    if (entry_index >= screen.entries.size()) {
        return;
    }
    const i32 max_value = std::max(0, UiScreenEntryI32(screen.entries[entry_index], 0x50));
    SetUiScreenEntryI32(screen.entries[entry_index], 0x54,
        std::clamp(value, 0, max_value));
}

i32 entry_scroll_value(const UiScreenDefinition& screen, u32 entry_index) {
    if (entry_index >= screen.entries.size()) {
        return 0;
    }
    return UiScreenEntryI32(screen.entries[entry_index], 0x54);
}

void populate_gameplay_options_sliders(
    GameplayModalUiState& state, UiScreenDefinition& screen) {
    set_entry_scroll_value(screen, 4, 0x0f - std::clamp(state.music_volume_left, 0, 0x0f));
    set_entry_scroll_value(screen, 5, 0x0f - std::clamp(state.music_volume_right, 0, 0x0f));
    set_entry_scroll_value(screen, 6,
        std::clamp(static_cast<int>(static_cast<u32>(state.music_volume_raw) >> 12),
            0, 0x0f));
    set_entry_scroll_value(screen, 7,
        std::clamp((state.sound_volume_raw + 10000) / 0x29a, 0, 0x0f));
    set_entry_scroll_value(screen, 8, state.scroll_speed);
}

void configure_gameplay_options_entries(
    GameplayModalUiState& state, UiScreenDefinition& screen) {
    if (state.generic_ai_profile_mode) {
        set_entry_enabled(screen, 4, false);
        set_entry_enabled(screen, 5, true);
        set_entry_enabled(screen, 8, true);
        set_entry_enabled(screen, 9, false);
        set_entry_enabled(screen, 10, true);
        set_entry_enabled(screen, 13, true);
    } else {
        set_entry_enabled(screen, 4, true);
        set_entry_enabled(screen, 5, true);
        set_entry_enabled(screen, 8, false);
        set_entry_enabled(screen, 9, true);
        set_entry_enabled(screen, 10, true);
        set_entry_enabled(screen, 13, false);
    }

    for (u32 entry : {6u, 7u, 11u, 12u}) {
        set_entry_enabled(screen, entry, state.sound_options_available);
    }

    set_entry_button_triplet(screen, 1, state.catchup_enabled ? 6 : 0x15,
        state.catchup_enabled ? 7 : 0x16);
    if (screen.entries.size() > 1) {
        SetUiScreenEntryI32(screen.entries[1], 0x10,
            state.catchup_enabled ? 'S' : 'F');
    }
    set_entry_button_triplet(screen, 14, state.unit_resource_pack_variant ? 0x19 : 0x17,
        state.unit_resource_pack_variant ? 0x1a : 0x18);
}

void capture_gameplay_options_sliders(
    GameplayModalUiState& state, const UiScreenDefinition& screen) {
    state.music_volume_left =
        0x0f - std::clamp(entry_scroll_value(screen, 4), 0, 0x0f);
    state.music_volume_right =
        0x0f - std::clamp(entry_scroll_value(screen, 5), 0, 0x0f);
    const i32 music_slider = std::clamp(entry_scroll_value(screen, 6), 0, 0x0f);
    state.music_volume_raw =
        (music_slider << 12) | (music_slider << 8) |
        (music_slider << 4) | music_slider;
    const i32 sound_slider = std::clamp(entry_scroll_value(screen, 7), 0, 0x0f);
    state.sound_volume_raw = (sound_slider * 10000) / 0x0f - 10000;
    state.scroll_speed = entry_scroll_value(screen, 8);
}

void initialize_and_release_slot(std::size_t slot) {
    UiScreenDefinition& screen = modal_screen(slot);
    HandleUiScreenDefinitionResourceRelease(screen);
    InitializeUiScreenDefinition(screen);
}

bool load_gameplay_modal_screen(GameplayModalUiState& state, std::size_t slot,
    u32 record_index, int flag_index = -1) {
    initialize_and_release_slot(slot);
    UiScreenDefinition& screen = modal_screen(slot);
    bool ok = false;
    if (state.callbacks.load_screen != nullptr) {
        ok = state.callbacks.load_screen(state, screen, record_index);
    }
    else {
        ok = HandleUiScreenDefinitionTrcImport(screen, kGameplayModalUiArchive, record_index);
    }
    if (!ok) {
        return false;
    }

    state.last_loaded_record = record_index;
    log_gameplay_modal_action(state, GameplayModalUiActionKind::ScreenLoaded, record_index,
        static_cast<u32>(slot));
    if (state.callbacks.center_screen != nullptr) {
        state.callbacks.center_screen(state, screen);
    }
    else {
        const u32 height = state.centered_for_replay ? state.fallback_center_height :
            state.center_height;
        HandleUiScreenEntriesCentering(screen, static_cast<i32>(state.center_width),
            static_cast<i32>(height));
    }
    if (flag_index >= 0) {
        set_gameplay_modal_flag(state, static_cast<std::size_t>(flag_index), true);
    }
    return true;
}

bool load_gameplay_modal_screen_from_archive(GameplayModalUiState& state, std::size_t slot,
    const char* archive_name, u32 record_index, int flag_index = -1, bool center = true) {
    initialize_and_release_slot(slot);
    UiScreenDefinition& screen = modal_screen(slot);
    bool ok = false;
    if (state.callbacks.load_screen != nullptr && archive_name == kGameplayModalUiArchive) {
        ok = state.callbacks.load_screen(state, screen, record_index);
    }
    else {
        ok = HandleUiScreenDefinitionTrcImport(screen, archive_name, record_index);
    }
    if (!ok) {
        return false;
    }
    state.last_loaded_record = record_index;
    log_gameplay_modal_action(state, GameplayModalUiActionKind::ScreenLoaded, record_index,
        static_cast<u32>(slot), archive_name != nullptr ? archive_name : "");
    if (center) {
        if (state.callbacks.center_screen != nullptr) {
            state.callbacks.center_screen(state, screen);
        }
        else {
            const u32 height = state.centered_for_replay ? state.fallback_center_height :
                state.center_height;
            HandleUiScreenEntriesCentering(screen, static_cast<i32>(state.center_width),
                static_cast<i32>(height));
        }
    }
    if (flag_index >= 0) {
        set_gameplay_modal_flag(state, static_cast<std::size_t>(flag_index), true);
    }
    return true;
}

void release_gameplay_modal_screen(GameplayModalUiState& state, std::size_t slot,
    int flag_index = -1) {
    UiScreenDefinition& screen = modal_screen(slot);
    if (state.callbacks.release_screen != nullptr) {
        state.callbacks.release_screen(state, screen);
    }
    else {
        HandleUiScreenDefinitionResourceRelease(screen);
    }
    if (flag_index >= 0) {
        set_gameplay_modal_flag(state, static_cast<std::size_t>(flag_index), false);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::ScreenReleased,
        static_cast<u32>(slot));
}

void draw_gameplay_modal_screen(GameplayModalUiState& state, UiScreenDefinition& screen) {
    if (state.callbacks.draw_screen != nullptr) {
        state.callbacks.draw_screen(state, screen);
    }
    else {
        HandleUiScreenDefinitionDraw(screen);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::DrawActiveScreen);
}

u32 run_gameplay_modal(GameplayModalUiState& state, UiScreenDefinition& screen) {
    int entry_state = 0;
    u32 activated = 0;
    if (state.callbacks.run_modal != nullptr) {
        activated = state.callbacks.run_modal(state, screen, entry_state);
    }
    else {
        RunUiScreenModalPump(screen, activated, entry_state);
    }
    state.last_activated_entry = activated;
    state.last_entry_state = entry_state;
    return activated;
}

u32 poll_or_run_gameplay_modal(GameplayModalUiState& state, UiScreenDefinition& screen) {
    int entry_state = 0;
    u32 activated = 0;
    if (state.generic_ai_profile_mode) {
        if (state.callbacks.poll_modal != nullptr) {
            activated = state.callbacks.poll_modal(state, screen, entry_state);
        }
        else if (!HandleUiScreenInputTick(screen, activated, entry_state)) {
            state.last_activated_entry = 0;
            state.last_entry_state = entry_state;
            return 0;
        }
    }
    else {
        if (state.callbacks.run_modal != nullptr) {
            activated = state.callbacks.run_modal(state, screen, entry_state);
        }
        else {
            PushBackSurfaceSnapshot();
            RunUiScreenModalPump(screen, activated, entry_state);
            PopBackSurfaceSnapshot();
        }
    }
    state.last_activated_entry = activated;
    state.last_entry_state = entry_state;
    return activated;
}

void set_cursor_for_gameplay_modal() {
#ifdef _WIN32
    SetGameCursorIndex(0);
#endif
}

std::string save_slot_label(const GameplayModalUiState& state, u32 slot) {
    if (slot >= state.save_slots.size()) {
        return {};
    }
    const GameplayModalSaveSlot& save_slot = state.save_slots[slot];
    switch (save_slot.state) {
    case GameplayModalSaveSlotState::Empty:
        return state.empty_save_label;
    case GameplayModalSaveSlotState::Occupied:
        return save_slot.label.empty() ? state.empty_save_label : save_slot.label;
    case GameplayModalSaveSlotState::Invalid:
        return state.invalid_save_label;
    default:
        return {};
    }
}

std::string save_slot_edit_label(const GameplayModalUiState& state, u32 slot) {
    if (slot >= state.save_slots.size()) {
        return {};
    }
    const GameplayModalSaveSlot& save_slot = state.save_slots[slot];
    return save_slot.state == GameplayModalSaveSlotState::Occupied ?
        save_slot.label : std::string{};
}

void refresh_save_slot_screen(GameplayModalUiState& state, UiScreenDefinition& screen) {
    for (u32 slot = 0; slot < kGameplayModalSaveSlotCount; ++slot) {
        const u32 entry_index = slot + 1;
        if (entry_index >= screen.entries.size()) {
            continue;
        }
        SetUiScreenEntryI32(screen.entries[entry_index], 8, 4);
        SetUiScreenEntryI32(screen.entries[entry_index], 0x0c, 4);
        set_entry_text_safe(screen, entry_index, save_slot_label(state, slot));
        set_entry_flags_bit(screen, entry_index, 1, slot == state.selected_save_slot);
    }
}

void refresh_save_slot_selection_flags(GameplayModalUiState& state,
    UiScreenDefinition& screen) {
    for (u32 slot = 0; slot < kGameplayModalSaveSlotCount; ++slot) {
        const u32 entry_index = slot + 1;
        if (entry_index >= screen.entries.size()) {
            continue;
        }
        set_entry_flags_bit(screen, entry_index, 1, slot == state.selected_save_slot);
    }
}

bool selected_save_slot_occupied(const GameplayModalUiState& state) {
    const u32 slot = std::min<u32>(state.selected_save_slot,
        static_cast<u32>(kGameplayModalSaveSlotCount - 1));
    return state.save_slots[slot].state == GameplayModalSaveSlotState::Occupied;
}

bool import_selected_save_slot(GameplayModalUiState& state, bool reload_profiles) {
    const u32 slot = std::min<u32>(state.selected_save_slot,
        static_cast<u32>(kGameplayModalSaveSlotCount - 1));
    if (state.save_slots[slot].state != GameplayModalSaveSlotState::Occupied) {
        return false;
    }

    bool ok = true;
    if (state.callbacks.import_session_bundle != nullptr) {
        ok = state.callbacks.import_session_bundle(state, slot);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::SessionImportRequested, slot,
        ok ? 1u : 0u);
    if (!ok) {
        return false;
    }

    if (reload_profiles && state.callbacks.reload_skirmish_profiles != nullptr) {
        state.callbacks.reload_skirmish_profiles(state);
    }
    if (state.callbacks.clear_reliable_packet_rings != nullptr) {
        state.callbacks.clear_reliable_packet_rings(state);
    }
    if (state.callbacks.import_runtime_tables != nullptr) {
        state.callbacks.import_runtime_tables(state);
    }
    if (state.non_empty_runtime_tables_available &&
        state.callbacks.import_non_empty_runtime_tables != nullptr) {
        state.callbacks.import_non_empty_runtime_tables(state);
    }
    if (state.callbacks.rebuild_unit_type_references != nullptr) {
        state.callbacks.rebuild_unit_type_references(state);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::RuntimeTablesImported);
    return true;
}

bool export_selected_save_slot(GameplayModalUiState& state, UiScreenDefinition& screen) {
    const u32 slot = std::min<u32>(state.selected_save_slot,
        static_cast<u32>(kGameplayModalSaveSlotCount - 1));
    const u32 entry_index = slot + 1;
    if (entry_index < screen.entries.size()) {
        state.save_slots[slot].label = entry_text(screen.entries[entry_index]);
    }

    bool ok = true;
    if (state.callbacks.export_session_bundle != nullptr) {
        ok = state.callbacks.export_session_bundle(state, slot);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::SessionExportRequested, slot,
        ok ? 1u : 0u);
    if (!ok) {
        OpenGameplayMessageDialog(state, state.export_error_message.c_str());
        return false;
    }

    state.save_slots[slot].state = GameplayModalSaveSlotState::Occupied;
    refresh_save_slot_screen(state, screen);
    return true;
}

bool open_load_session_dialog(GameplayModalUiState& state, std::size_t slot, u32 record_index,
    bool reload_profiles) {
    bool failed_once = false;

    for (;;) {
        set_cursor_for_gameplay_modal();
        if (state.callbacks.scan_save_slot_headers != nullptr) {
            state.callbacks.scan_save_slot_headers(state);
        }
        if (!load_gameplay_modal_screen(state, slot, record_index)) {
            return false;
        }

        UiScreenDefinition& screen = modal_screen(slot);
        refresh_save_slot_screen(state, screen);
        while (true) {
            refresh_save_slot_selection_flags(state, screen);
            const u32 activated = run_gameplay_modal(state, screen);
            if (activated == 0) {
                continue;
            }
            if (1 <= activated && activated <= kGameplayModalSaveSlotCount) {
                state.selected_save_slot = activated - 1;
                continue;
            }
            if (activated == 9) {
                if (!selected_save_slot_occupied(state)) {
                    continue;
                }
                release_gameplay_modal_screen(state, slot);
                if (import_selected_save_slot(state, reload_profiles)) {
                    return true;
                }
                failed_once = true;
                OpenGameplayMessageDialog(state, state.import_error_message.c_str());
                break;
            }
            if (activated == 10) {
                if (failed_once && state.require_cancel_confirmation) {
                    OpenGameplayMessageDialog(state, state.cancel_confirm_message.c_str());
                    continue;
                }
                release_gameplay_modal_screen(state, slot);
                return false;
            }
            release_gameplay_modal_screen(state, slot);
            return false;
        }
    }
}

UiScreenDefinition* active_gameplay_modal_screen(GameplayModalUiState& state) {
    if (state.wait_dialog_active) {
        return &modal_screen(kGameplayModalWaitSlot);
    }
    if (state.exit_surrender_active) {
        return &modal_screen(kGameplayModalExitSurrenderSlot);
    }
    if (state.scenario_message_active) {
        return &modal_screen(kGameplayModalScenarioMessageSlot);
    }
    if (state.options_active) {
        return &modal_screen(kGameplayModalOptionsSlot);
    }
    if (state.relation_mask_active) {
        return &modal_screen(kGameplayModalRelationSlot);
    }
    if (state.observer_mask_active) {
        return &modal_screen(kGameplayModalObserverSlot);
    }
    if (state.main_menu_active) {
        return &modal_screen(kGameplayModalMainSlot);
    }
    return nullptr;
}

bool player_slot_disabled_for_relation(const GameplayModalPlayerSlot& player) {
    return player.slot_state == 0x14 || player.slot_state == 2;
}

u8 local_player_slot_state(const GameplayModalUiState& state) {
    return state.local_player_index < kGameplayModalPlayerSlots ?
        state.players[state.local_player_index].slot_state : 0;
}

u8 local_player_modal_pause_uses_remaining(const GameplayModalUiState& state) {
    return state.local_player_index < kGameplayModalPlayerSlots ?
        state.players[state.local_player_index].modal_pause_uses_remaining : 0;
}

bool scenario_modal_uses_default_objective_text(const GameplayModalUiState& state) {
    return state.generic_ai_profile_mode ||
        (state.network_ai_profile_override && state.session_mode != 5);
}

std::string scenario_modal_objective_text(const GameplayModalUiState& state) {
    if (scenario_modal_uses_default_objective_text(state)) {
        return state.default_objective_text;
    }
    return !state.scenario_message_text.empty() ?
        state.scenario_message_text : state.fallback_scenario_message_text;
}

bool pause_menu_modal_pause_available(const GameplayModalUiState& state) {
    if (state.scenario_ai_profile_override || !state.generic_ai_profile_mode) {
        return false;
    }
    return state.modal_pause_suppressed ||
        local_player_modal_pause_uses_remaining(state) != 0;
}

void configure_pause_menu_modal_pause_entry(
    GameplayModalUiState& state, UiScreenDefinition& screen) {
    constexpr u32 kEntry = 7;
    if (kEntry >= screen.entries.size()) {
        return;
    }

    const u8 local_pause_uses =
        local_player_modal_pause_uses_remaining(state);
    if (state.scenario_ai_profile_override || !state.generic_ai_profile_mode ||
        (!state.modal_pause_suppressed && local_pause_uses == 0)) {
        set_entry_enabled(screen, kEntry, false);
        set_entry_button_triplet(screen, kEntry, 0x15, 0x15);
        return;
    }

    set_entry_enabled(screen, kEntry, true);
    if (state.modal_pause_suppressed) {
        set_entry_button_triplet(screen, kEntry, 0x16, 0x17);
        SetUiScreenEntryI32(screen.entries[kEntry], 0x10, 'R');
        return;
    }

    set_entry_button_triplet(screen, kEntry, 0x13, 0x14);
    SetUiScreenEntryI32(screen.entries[kEntry], 0x10, 'P');
}

u32 observer_toggle_player_index(const GameplayModalUiState& state, u32 compact_index) {
    u32 player = compact_index;
    if (state.local_player_index <= player) {
        ++player;
    }
    return std::min<u32>(player, static_cast<u32>(kGameplayModalPlayerSlots - 1));
}

void toggle_low_observer_flags(GameplayModalUiState& state) {
    if ((state.compact_observer_flags & 1u) == 0) {
        state.compact_observer_flags |= 0x0fu;
    }
    else {
        state.compact_observer_flags &= 0xfffffff0u;
    }
}

void send_replay_modal_action(GameplayModalUiState& state, u32 action) {
    state.replay_modal_pending = true;
    if (state.callbacks.send_replay_modal_action != nullptr) {
        state.callbacks.send_replay_modal_action(state, action);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::MessageShown, action, 0,
        "replay-modal-action");
    state.replay_modal_pending = false;
}

} // namespace

GlobalUiScreenSlotsState& global_ui_screen_slots_state() {
    return g_global_ui_screen_slots;
}

UiScreenDefinition& GlobalUiScreenSlot(std::size_t slot) {
    if (!valid_global_ui_screen_slot(slot)) {
        slot = 0;
    }
    return g_global_ui_screen_slots.screens[slot];
}

void InitializeGlobalUiScreenSlotDefinition(std::size_t slot) {
    if (!valid_global_ui_screen_slot(slot)) {
        return;
    }
    InitializeUiScreenDefinition(g_global_ui_screen_slots.screens[slot]);
}

void RegisterGlobalUiScreenSlotShutdown(std::size_t slot) {
    if (!valid_global_ui_screen_slot(slot) ||
        g_global_ui_screen_slots.shutdown_registered[slot]) {
        return;
    }
    std::atexit(kGlobalUiScreenSlotShutdowns[slot]);
    g_global_ui_screen_slots.shutdown_registered[slot] = true;
}

void ReleaseGlobalUiScreenSlotDefinition(std::size_t slot) {
    release_global_ui_screen_slot(slot);
}

void InitializeGlobalUiScreenSlotSupport(std::size_t slot) {
    InitializeGlobalUiScreenSlotDefinition(slot);
    RegisterGlobalUiScreenSlotShutdown(slot);
}

void SetGlobalUiScreenModalResourceActive(std::size_t flag, bool active) {
    if (flag >= kGlobalUiScreenModalFlagCount) {
        return;
    }
    g_global_ui_screen_slots.modal_resource_active[flag] = active;
}

void ReleaseActiveGlobalUiScreenModalResources() {
    static constexpr std::array<std::size_t, kGlobalUiScreenModalFlagCount> kReleaseOrder = {
        6, 2, 1, 3, 4, 5, 0,
    };

    for (std::size_t flag : kReleaseOrder) {
        if (g_global_ui_screen_slots.modal_resource_active[flag]) {
            ReleaseGlobalUiScreenSlotDefinition(flag);
        }
    }
    g_global_ui_screen_slots.modal_resource_active.fill(false);
}

#define RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(Suffix, Index) \
    void InitializeGlobalUiScreenSlot##Suffix##Support() { \
        InitializeGlobalUiScreenSlot##Suffix##Definition(); \
        RegisterGlobalUiScreenSlot##Suffix##Shutdown(); \
    } \
    void InitializeGlobalUiScreenSlot##Suffix##Definition() { \
        InitializeGlobalUiScreenSlotDefinition(Index); \
    } \
    void RegisterGlobalUiScreenSlot##Suffix##Shutdown() { \
        RegisterGlobalUiScreenSlotShutdown(Index); \
    } \
    void ReleaseGlobalUiScreenSlot##Suffix##Definition() { \
        ReleaseGlobalUiScreenSlotDefinition(Index); \
    }

RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(00, 0)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(01, 1)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(02, 2)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(03, 3)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(04, 4)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(05, 5)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(06, 6)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(07, 7)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(08, 8)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(09, 9)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(10, 10)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(11, 11)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(12, 12)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(13, 13)
RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS(14, 14)

#undef RANKER_DEFINE_GLOBAL_UI_SCREEN_SLOT_WRAPPERS

GameplayModalUiState& gameplay_modal_ui_state() {
    return g_gameplay_modal_ui_state;
}

void PumpActiveGameplayModalUiFlow(GameplayModalUiState& state) {
    if (state.wait_dialog_active) {
        bool complete = false;
        if (state.callbacks.pump_wait_dialog != nullptr) {
            complete = state.callbacks.pump_wait_dialog(state);
        }
        else {
            complete = PollGameplayWaitDialog(state);
        }
        if (complete) {
            const u32 mask = BuildGameplayWaitConsensusMask(state);
            if (state.local_player_index < kGameplayModalPlayerSlots) {
                state.players[state.local_player_index].visibility_mask = mask;
            }
            if (state.callbacks.publish_corrective_checksum != nullptr) {
                state.callbacks.publish_corrective_checksum(state);
            }
            log_gameplay_modal_action(state,
                GameplayModalUiActionKind::CorrectiveChecksumPublished);
            log_gameplay_modal_action(state,
                GameplayModalUiActionKind::WaitConsensusMaskPublished, mask);
        }
        return;
    }

    if (state.exit_surrender_active) {
        const bool close_main = OpenGameplayExitSurrenderDialog(state);
        if (state.main_menu_active && close_main) {
            release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                static_cast<int>(kGameplayModalMainFlag));
        }
        return;
    }
    if (state.scenario_message_active) {
        OpenGameplayScenarioMessageDialog(state);
        return;
    }
    if (state.options_active) {
        const bool close_main = OpenGameplayOptionsDialog(state);
        if (state.main_menu_active && close_main) {
            release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                static_cast<int>(kGameplayModalMainFlag));
        }
        return;
    }
    if (state.relation_mask_active) {
        OpenGameplayRelationMaskDialog(state);
        return;
    }
    if (state.observer_mask_active) {
        OpenGameplayObserverMaskDialog(state);
        return;
    }
    if (state.main_menu_active) {
        OpenGameplayPauseMenu(state);
    }
}

void PumpActiveGameplayModalUiFlow() {
    PumpActiveGameplayModalUiFlow(gameplay_modal_ui_state());
}

void DrawActiveGameplayModalUiScreen(GameplayModalUiState& state) {
    if (UiScreenDefinition* screen = active_gameplay_modal_screen(state)) {
        draw_gameplay_modal_screen(state, *screen);
    }
}

void DrawActiveGameplayModalUiScreen() {
    DrawActiveGameplayModalUiScreen(gameplay_modal_ui_state());
}

void OpenLocalNetworkAddressDialogWithInputReset(GameplayModalUiState& state) {
    ResetInputState();
    OpenLocalNetworkAddressDialog(state);
}

void OpenLocalNetworkAddressDialogWithInputReset() {
    OpenLocalNetworkAddressDialogWithInputReset(gameplay_modal_ui_state());
}

void OpenLocalNetworkAddressDialog(GameplayModalUiState& state) {
    UiScreenDefinition& screen = modal_screen(kGameplayModalLocalAddressSlot);
    initialize_and_release_slot(kGameplayModalLocalAddressSlot);

    const u32 address = state.local_network_address;
    std::array<unsigned, 4> octets = {
        address & 0xffu,
        (address >> 8) & 0xffu,
        (address >> 16) & 0xffu,
        (address >> 24) & 0xffu,
    };
    char buffer[64]{};
    const char* format = state.network_address_format.empty() ?
        "Ver %d-%d-%d" : state.network_address_format.c_str();
    std::snprintf(buffer, sizeof(buffer), format,
        static_cast<int>(address & 0xffffu), static_cast<int>(octets[2]),
        static_cast<int>(octets[3]));
    state.network_address_text = buffer;

    if (!load_gameplay_modal_screen(state, kGameplayModalLocalAddressSlot, 0x5e)) {
        return;
    }
    set_entry_text_safe(screen, 6, state.network_address_text);
    if (screen.entries.size() > 6) {
        SetUiScreenEntryI32(screen.entries[6], 8, 1);
        SetUiScreenEntryI32(screen.entries[6], 0x0c, 3);
    }
    run_gameplay_modal(state, screen);
    release_gameplay_modal_screen(state, kGameplayModalLocalAddressSlot);
    HandleUiScreenDefinitionReleaseWrapper(screen);
}

void OpenLocalNetworkAddressDialog() {
    OpenLocalNetworkAddressDialog(gameplay_modal_ui_state());
}

void OpenStageAvailabilityDialog(GameplayModalUiState& state) {
    if (!load_gameplay_modal_screen(state, kGameplayModalStageAvailabilitySlot, 0x5f)) {
        return;
    }
    UiScreenDefinition& screen = modal_screen(kGameplayModalStageAvailabilitySlot);
    if (!state.stage_archive_present) {
        for (u32 entry = 5; entry < 9; ++entry) {
            set_entry_enabled(screen, entry, false);
        }
    }

    state.stage_hover_hint = 0x25;
    if (screen.entries.size() > 9) {
        SetUiScreenEntryI32(screen.entries[9], 0x3c, static_cast<i32>(state.stage_hover_hint));
        SetUiScreenEntryI32(screen.entries[9], 4, 4);
    }

    while (true) {
        u32 activated = 0;
        int entry_state = 0;
        if (state.callbacks.poll_modal != nullptr) {
            activated = state.callbacks.poll_modal(state, screen, entry_state);
            state.last_activated_entry = activated;
            state.last_entry_state = entry_state;
            if (activated != 0) {
                break;
            }
        }
        else if (HandleUiScreenInputTick(screen, activated, entry_state)) {
            state.last_activated_entry = activated;
            state.last_entry_state = entry_state;
            break;
        }

        draw_gameplay_modal_screen(state, screen);
        u32 hint = 0x25;
        for (u32 entry = 5; entry < 9 && entry < screen.entries.size(); ++entry) {
            const i32 state_value = UiScreenEntryI32(screen.entries[entry], 0);
            if (state_value == 1 || state_value == 2) {
                hint = entry + 0x1c;
                break;
            }
        }
        for (u32 entry = 1; hint == 0x25 && entry < 5 && entry < screen.entries.size(); ++entry) {
            const i32 state_value = UiScreenEntryI32(screen.entries[entry], 0);
            if (state_value == 1 || state_value == 2) {
                hint = 0x25;
                break;
            }
        }
        state.stage_hover_hint = hint;
        if (screen.entries.size() > 9) {
            SetUiScreenEntryI32(screen.entries[9], 0x3c, static_cast<i32>(hint));
            SetUiScreenEntryI32(screen.entries[9], 4, 4);
        }
    }
    release_gameplay_modal_screen(state, kGameplayModalStageAvailabilitySlot);
}

void OpenStageAvailabilityDialog() {
    OpenStageAvailabilityDialog(gameplay_modal_ui_state());
}

bool OpenSkirmishLoadSessionDialog(GameplayModalUiState& state) {
    return open_load_session_dialog(state, kGameplayModalSkirmishLoadSlot, 0x60, true);
}

bool OpenSkirmishLoadSessionDialog() {
    return OpenSkirmishLoadSessionDialog(gameplay_modal_ui_state());
}

void OpenGameplaySimpleInfoDialog(GameplayModalUiState& state) {
    if (!load_gameplay_modal_screen(state, kGameplayModalSimpleInfoSlot, 0x62)) {
        return;
    }
    UiScreenDefinition& screen = modal_screen(kGameplayModalSimpleInfoSlot);
    run_gameplay_modal(state, screen);
    release_gameplay_modal_screen(state, kGameplayModalSimpleInfoSlot);
}

void OpenGameplaySimpleInfoDialog() {
    OpenGameplaySimpleInfoDialog(gameplay_modal_ui_state());
}

void OpenGameplayIndexedSelectionDialog(GameplayModalUiState& state, u32 selected_index) {
    if (selected_index >= 4) {
        return;
    }
    set_cursor_for_gameplay_modal();
    if (!load_gameplay_modal_screen(state, kGameplayModalIndexedSelectionSlot, 0x65)) {
        return;
    }
    UiScreenDefinition& screen = modal_screen(kGameplayModalIndexedSelectionSlot);
    for (u32 i = 0; i < 4; ++i) {
        set_entry_flags_bit(screen, 2 + i, 4, i == selected_index);
    }
    run_gameplay_modal(state, screen);
    release_gameplay_modal_screen(state, kGameplayModalIndexedSelectionSlot);
}

void OpenGameplayIndexedSelectionDialog(u32 selected_index) {
    OpenGameplayIndexedSelectionDialog(gameplay_modal_ui_state(), selected_index);
}

bool OpenGameplayPauseMenu(GameplayModalUiState& state) {
    const bool previous_music_paused = state.music_paused;
    state.previous_music_paused = previous_music_paused;
    state.music_paused = true;
    if (state.callbacks.pause_music != nullptr) {
        state.callbacks.pause_music(state);
    }
    else {
        PausePrimaryMusicFromPolicy();
    }

    bool reload_menu = false;
    bool update_catchup_on_close = true;
    do {
        reload_menu = false;
        if (!state.main_menu_active) {
            if (!load_gameplay_modal_screen(state, kGameplayModalMainSlot,
                    selected_faction_record(state, 0x66),
                    static_cast<int>(kGameplayModalMainFlag))) {
                break;
            }
            UiScreenDefinition& screen = modal_screen(kGameplayModalMainSlot);
            set_entry_enabled(screen, 1, !state.generic_ai_profile_mode);
            set_entry_enabled(screen, 2, !state.generic_ai_profile_mode);
            configure_pause_menu_modal_pause_entry(state, screen);
        }

        set_cursor_for_gameplay_modal();
        UiScreenDefinition& screen = modal_screen(kGameplayModalMainSlot);
        const u32 activated = poll_or_run_gameplay_modal(state, screen);
        if (activated == 0 && state.generic_ai_profile_mode) {
            update_catchup_on_close = false;
            goto close_pause_menu;
        }

        switch (activated) {
        case 1:
            if (OpenGameplaySaveSessionDialog(state)) {
                release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                    static_cast<int>(kGameplayModalMainFlag));
                goto close_pause_menu;
            }
            break;
        case 2:
            release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                static_cast<int>(kGameplayModalMainFlag));
            if (OpenGameplayLoadSessionDialog(state)) {
                goto close_pause_menu;
            }
            reload_menu = true;
            break;
        case 3:
            if (OpenGameplayOptionsDialog(state)) {
                release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                    static_cast<int>(kGameplayModalMainFlag));
                goto close_pause_menu;
            }
            break;
        case 4:
            if (OpenGameplayScenarioMessageDialog(state)) {
                release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                    static_cast<int>(kGameplayModalMainFlag));
                goto close_pause_menu;
            }
            break;
        case 5:
            if (state.scenario_ai_profile_override) {
                state.quit_to_frontend_requested = true;
                log_gameplay_modal_action(state, GameplayModalUiActionKind::QuitRequested);
                release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                    static_cast<int>(kGameplayModalMainFlag));
                goto close_pause_menu;
            }
            if (OpenGameplayExitSurrenderDialog(state)) {
                release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                    static_cast<int>(kGameplayModalMainFlag));
                goto close_pause_menu;
            }
            break;
        case 7:
            if (pause_menu_modal_pause_available(state)) {
                if (state.callbacks.publish_modal_pause != nullptr) {
                    state.callbacks.publish_modal_pause(state);
                }
                log_gameplay_modal_action(state, GameplayModalUiActionKind::ModalPausePublished);
            }
            release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                static_cast<int>(kGameplayModalMainFlag));
            goto close_pause_menu;
        default:
            release_gameplay_modal_screen(state, kGameplayModalMainSlot,
                static_cast<int>(kGameplayModalMainFlag));
            goto close_pause_menu;
        }
    } while (reload_menu);

close_pause_menu:
    state.music_paused = previous_music_paused;
    if (!previous_music_paused) {
        if (state.callbacks.resume_music != nullptr) {
            state.callbacks.resume_music(state);
        }
        else {
            HandlePrimaryMusicPolicyResume();
        }
    }
    else if (state.callbacks.pause_music != nullptr) {
        state.callbacks.pause_music(state);
    }
    else {
        PausePrimaryMusicFromPolicy();
    }
    if (update_catchup_on_close && state.callbacks.update_catchup_target != nullptr) {
        state.callbacks.update_catchup_target(state);
    }
    return !state.main_menu_active;
}

bool OpenGameplayPauseMenu() {
    return OpenGameplayPauseMenu(gameplay_modal_ui_state());
}

bool OpenGameplayLoadSessionDialog(GameplayModalUiState& state) {
    return open_load_session_dialog(state, kGameplayModalGameplayLoadSlot,
        selected_faction_record(state, 0x67), false);
}

bool OpenGameplayLoadSessionDialog() {
    return OpenGameplayLoadSessionDialog(gameplay_modal_ui_state());
}

void RefreshGameplaySaveSlotLabels(GameplayModalUiState& state, UiScreenDefinition& screen) {
    refresh_save_slot_screen(state, screen);
}

void RefreshGameplaySaveSlotLabels(GameplayModalUiState& state) {
    RefreshGameplaySaveSlotLabels(state, modal_screen(kGameplayModalGameplaySaveSlot));
}

void RefreshGameplaySaveSlotLabels() {
    RefreshGameplaySaveSlotLabels(gameplay_modal_ui_state());
}

bool OpenGameplaySaveSessionDialog(GameplayModalUiState& state) {
    set_cursor_for_gameplay_modal();
    if (state.callbacks.scan_save_slot_headers != nullptr) {
        state.callbacks.scan_save_slot_headers(state);
    }
    if (!load_gameplay_modal_screen(state, kGameplayModalGameplaySaveSlot,
            selected_faction_record(state, 0x68))) {
        return false;
    }

    UiScreenDefinition& screen = modal_screen(kGameplayModalGameplaySaveSlot);
    RefreshGameplaySaveSlotLabels(state, screen);
    while (true) {
        refresh_save_slot_selection_flags(state, screen);
        const u32 activated = run_gameplay_modal(state, screen);
        if (activated == 0) {
            continue;
        }
        if (1 <= activated && activated <= kGameplayModalSaveSlotCount) {
            const u32 slot = activated - 1;
            if (state.selected_save_slot != slot) {
                state.selected_save_slot = slot;
                RefreshGameplaySaveSlotLabels(state, screen);
                const u32 entry_index = slot + 1;
                if (entry_index < screen.entries.size()) {
                    set_entry_text_safe(screen, entry_index,
                        save_slot_edit_label(state, slot));
                    append_text_cursor_marker(screen.entries[entry_index]);
                }
            }
            continue;
        }
        if (activated == 9) {
            if (export_selected_save_slot(state, screen)) {
                release_gameplay_modal_screen(state, kGameplayModalGameplaySaveSlot);
                return true;
            }
            continue;
        }
        if (activated == 10) {
            release_gameplay_modal_screen(state, kGameplayModalGameplaySaveSlot);
            return false;
        }
        release_gameplay_modal_screen(state, kGameplayModalGameplaySaveSlot);
        return false;
    }
}

bool OpenGameplaySaveSessionDialog() {
    return OpenGameplaySaveSessionDialog(gameplay_modal_ui_state());
}

bool OpenGameplayExitSurrenderDialog(GameplayModalUiState& state) {
    if (!state.exit_surrender_active) {
        if (!load_gameplay_modal_screen(state, kGameplayModalExitSurrenderSlot,
                selected_faction_record(state, 0x69),
                static_cast<int>(kGameplayModalExitSurrenderFlag))) {
            return false;
        }
        set_entry_enabled(modal_screen(kGameplayModalExitSurrenderSlot), 1,
            !state.generic_ai_profile_mode);
    }

    set_cursor_for_gameplay_modal();
    UiScreenDefinition& screen = modal_screen(kGameplayModalExitSurrenderSlot);
    const u32 activated = poll_or_run_gameplay_modal(state, screen);
    if (activated == 0 && state.generic_ai_profile_mode) {
        return false;
    }

    bool close_main = false;
    switch (activated) {
    case 1:
        state.end_session_requested = true;
        close_main = true;
        break;
    case 2:
        if (state.generic_ai_profile_mode) {
            if (state.callbacks.reset_and_publish_inactive != nullptr) {
                state.callbacks.reset_and_publish_inactive(state);
            }
            log_gameplay_modal_action(state, GameplayModalUiActionKind::PlayerInactivePublished);
        }
        else {
            state.quit_to_frontend_requested = true;
            log_gameplay_modal_action(state, GameplayModalUiActionKind::QuitRequested);
        }
        close_main = true;
        break;
    case 3:
        if (state.generic_ai_profile_mode) {
            if (state.callbacks.reset_and_publish_inactive != nullptr) {
                state.callbacks.reset_and_publish_inactive(state);
            }
            state.surrender_requested = true;
            log_gameplay_modal_action(state, GameplayModalUiActionKind::SurrenderRequested);
        }
        else {
            state.worker_exit_requested = true;
            if (state.callbacks.exit_worker_thread != nullptr) {
                state.callbacks.exit_worker_thread(state);
            }
            log_gameplay_modal_action(state, GameplayModalUiActionKind::WorkerExitRequested);
        }
        close_main = true;
        break;
    case 4:
        close_main = false;
        break;
    default:
        close_main = true;
        break;
    }

    release_gameplay_modal_screen(state, kGameplayModalExitSurrenderSlot,
        static_cast<int>(kGameplayModalExitSurrenderFlag));
    return close_main;
}

bool OpenGameplayExitSurrenderDialog() {
    return OpenGameplayExitSurrenderDialog(gameplay_modal_ui_state());
}

void OpenGameplayMessageDialog(GameplayModalUiState& state, const char* message) {
    const std::string text = message != nullptr ? message : state.default_message_text;
    if (state.callbacks.show_message != nullptr) {
        state.callbacks.show_message(state, text.c_str());
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::MessageShown, 0, 0, text);
    if (!load_gameplay_modal_screen(state, kGameplayModalScenarioMessageSlot,
            selected_faction_record(state, 0x6a))) {
        return;
    }
    UiScreenDefinition& screen = modal_screen(kGameplayModalScenarioMessageSlot);
    set_entry_text_safe(screen, 3, text);
    run_gameplay_modal(state, screen);
    release_gameplay_modal_screen(state, kGameplayModalScenarioMessageSlot);
}

void OpenGameplayMessageDialog(const char* message) {
    OpenGameplayMessageDialog(gameplay_modal_ui_state(), message);
}

bool OpenGameplayScenarioMessageDialog(GameplayModalUiState& state) {
    if (!state.scenario_message_active) {
        if (!load_gameplay_modal_screen(state, kGameplayModalScenarioMessageSlot,
                selected_faction_record(state, 0x6b),
                static_cast<int>(kGameplayModalScenarioMessageFlag))) {
            return false;
        }
        UiScreenDefinition& screen = modal_screen(kGameplayModalScenarioMessageSlot);
        if (screen.entries.size() > 2) {
            SetUiScreenEntryI32(screen.entries[2], 4, 0x1000);
            SetUiScreenEntryI32(screen.entries[2], 8, 2);
            SetUiScreenEntryI32(screen.entries[2], 0x0c, 3);
            set_entry_text_safe(screen, 2, scenario_modal_objective_text(state));
        }
    }
    UiScreenDefinition& screen = modal_screen(kGameplayModalScenarioMessageSlot);
    const u32 activated = poll_or_run_gameplay_modal(state, screen);
    if (activated == 0 && state.generic_ai_profile_mode) {
        return false;
    }
    release_gameplay_modal_screen(state, kGameplayModalScenarioMessageSlot,
        static_cast<int>(kGameplayModalScenarioMessageFlag));
    return activated != 0;
}

bool OpenGameplayScenarioMessageDialog() {
    return OpenGameplayScenarioMessageDialog(gameplay_modal_ui_state());
}

bool OpenGameplayOptionsDialog(GameplayModalUiState& state) {
    const bool previous_music_paused = state.music_paused;
    state.music_paused = true;
    if (state.callbacks.pause_music != nullptr) {
        state.callbacks.pause_music(state);
    }
    else {
        PausePrimaryMusicFromPolicy();
    }
    auto restore_music = [&]() {
        state.music_paused = previous_music_paused;
        if (!previous_music_paused) {
            if (state.callbacks.resume_music != nullptr) {
                state.callbacks.resume_music(state);
            }
            else {
                HandlePrimaryMusicPolicyResume();
            }
        }
        else if (state.callbacks.pause_music != nullptr) {
            state.callbacks.pause_music(state);
        }
        else {
            PausePrimaryMusicFromPolicy();
        }
    };

    if (!state.options_active) {
        if (!load_gameplay_modal_screen(state, kGameplayModalOptionsSlot,
                selected_faction_record(state, 0x6c),
                static_cast<int>(kGameplayModalOptionsFlag))) {
            restore_music();
            return false;
        }
        UiScreenDefinition& screen = modal_screen(kGameplayModalOptionsSlot);
        configure_gameplay_options_entries(state, screen);
        populate_gameplay_options_sliders(state, screen);
    }

    set_cursor_for_gameplay_modal();
    bool close_main = false;
    while (true) {
        UiScreenDefinition& screen = modal_screen(kGameplayModalOptionsSlot);
        const u32 activated = poll_or_run_gameplay_modal(state, screen);
        if (activated == 0 && state.generic_ai_profile_mode) {
            restore_music();
            return false;
        }
        switch (activated) {
        case 1:
            if (!state.scenario_ai_profile_override) {
                state.catchup_enabled = !state.catchup_enabled;
                if (state.callbacks.toggle_catchup != nullptr) {
                    state.callbacks.toggle_catchup(state);
                }
                log_gameplay_modal_action(state, GameplayModalUiActionKind::CatchupToggled,
                    state.catchup_enabled ? 1u : 0u);
            }
            close_main = true;
            goto close_options;
        case 2:
            capture_gameplay_options_sliders(state, screen);
            if (state.callbacks.apply_music_volume != nullptr) {
                state.callbacks.apply_music_volume(state);
            }
            else {
                HandlePrimaryMusicPolicyVolumeApply();
            }
            if (state.callbacks.apply_setup_data != nullptr) {
                state.callbacks.apply_setup_data(state);
            }
            log_gameplay_modal_action(state, GameplayModalUiActionKind::SetupDataApplied);
            close_main = true;
            goto close_options;
        case 3:
            close_main = false;
            goto close_options;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            break;
        case 0x0e:
            state.unit_resource_pack_variant = !state.unit_resource_pack_variant;
            if (state.callbacks.toggle_unit_resource_pack != nullptr) {
                state.callbacks.toggle_unit_resource_pack(state);
            }
            else {
                bool setup_write_requested = false;
                ToggleUnitResourcePackVariantAndReload(&setup_write_requested);
            }
            log_gameplay_modal_action(state,
                GameplayModalUiActionKind::UnitResourcePackToggled,
                state.unit_resource_pack_variant ? 1u : 0u);
            close_main = true;
            goto close_options;
        default:
            close_main = true;
            goto close_options;
        }
    }

close_options:
    release_gameplay_modal_screen(state, kGameplayModalOptionsSlot,
        static_cast<int>(kGameplayModalOptionsFlag));
    restore_music();
    return close_main;
}

bool OpenGameplayOptionsDialog() {
    return OpenGameplayOptionsDialog(gameplay_modal_ui_state());
}

void RefreshGameplayRelationMaskDialogControls(GameplayModalUiState& state,
    u32 relation_mask, u32 visibility_mask, bool observer_flag) {
    state.pending_relation_mask = relation_mask;
    state.pending_visibility_mask = visibility_mask;
    state.relation_observer_flag = observer_flag;
    state.relation_controls.clear();

    UiScreenDefinition& screen = modal_screen(kGameplayModalRelationSlot);
    for (u32 player = 0; player < kGameplayModalPlayerSlots; ++player) {
        const bool disabled = player_slot_disabled_for_relation(state.players[player]);
        const bool local = player == state.local_player_index;
        const u32 relation_entry = 3 + player;
        const u32 visibility_entry = 0x0b + player;
        const u32 both_entry = 0x13 + player;

        GameplayModalUiControlState relation_control{};
        relation_control.entry_index = relation_entry;
        relation_control.enabled = !disabled && !local;
        relation_control.state = (relation_mask & (1u << player)) != 0 ? 1 : 0;
        relation_control.text = state.players[player].display_name;
        state.relation_controls.push_back(relation_control);

        if (relation_entry < screen.entries.size()) {
            set_entry_enabled(screen, relation_entry, relation_control.enabled);
            set_entry_button_triplet(screen, relation_entry,
                relation_control.state ? 6 : 5, relation_control.state ? 5 : 6);
        }
        if (visibility_entry < screen.entries.size()) {
            set_entry_enabled(screen, visibility_entry, !disabled && !local);
            const bool on = (visibility_mask & (1u << player)) != 0;
            set_entry_button_triplet(screen, visibility_entry, on ? 6 : 5, on ? 5 : 6);
        }
        if (both_entry < screen.entries.size()) {
            set_entry_enabled(screen, both_entry, !disabled && !local);
            set_entry_text_safe(screen, both_entry, state.players[player].display_name);
        }
    }
    if (screen.entries.size() > 0x23) {
        set_entry_button_triplet(screen, 0x23, observer_flag ? 6 : 5, observer_flag ? 5 : 6);
        if (player_slot_disabled_for_relation(state.players[state.local_player_index])) {
            set_entry_enabled(screen, 0x23, false);
        }
    }
}

void RefreshGameplayRelationMaskDialogControls(u32 relation_mask, u32 visibility_mask,
    bool observer_flag) {
    RefreshGameplayRelationMaskDialogControls(gameplay_modal_ui_state(), relation_mask,
        visibility_mask, observer_flag);
}

bool OpenGameplayRelationMaskDialog(GameplayModalUiState& state) {
    if (state.scenario_ai_profile_override || !state.generic_ai_profile_mode ||
        state.transport_mode == 2 || state.transport_mode == 4) {
        return false;
    }
    if (!state.relation_mask_active) {
        if (!load_gameplay_modal_screen(state, kGameplayModalRelationSlot,
                selected_faction_record(state, 0x6d),
                static_cast<int>(kGameplayModalRelationFlag))) {
            return false;
        }
        if (state.local_player_index < kGameplayModalPlayerSlots) {
            state.pending_relation_mask = state.players[state.local_player_index].relation_mask;
            state.pending_visibility_mask = state.players[state.local_player_index].visibility_mask;
        }
        RefreshGameplayRelationMaskDialogControls(state, state.pending_relation_mask,
            state.pending_visibility_mask, state.relation_observer_flag);
    }

    set_cursor_for_gameplay_modal();
    UiScreenDefinition& screen = modal_screen(kGameplayModalRelationSlot);
    while (true) {
        const u32 activated = poll_or_run_gameplay_modal(state, screen);
        if (activated == 0 && state.generic_ai_profile_mode) {
            return false;
        }
        if (activated == 1) {
            if (state.callbacks.publish_relation_mask != nullptr) {
                state.callbacks.publish_relation_mask(state, state.pending_relation_mask,
                    state.pending_visibility_mask, state.relation_observer_flag);
            }
            log_gameplay_modal_action(state, GameplayModalUiActionKind::RelationMaskPublished,
                state.pending_relation_mask, state.pending_visibility_mask);
            release_gameplay_modal_screen(state, kGameplayModalRelationSlot,
                static_cast<int>(kGameplayModalRelationFlag));
            return true;
        }
        if (activated == 2) {
            release_gameplay_modal_screen(state, kGameplayModalRelationSlot,
                static_cast<int>(kGameplayModalRelationFlag));
            return false;
        }
        if (3 <= activated && activated <= 0x0a) {
            state.pending_relation_mask ^= 1u << (activated - 3);
        }
        else if (0x0b <= activated && activated <= 0x12) {
            state.pending_visibility_mask ^= 1u << (activated - 0x0b);
        }
        else if (0x13 <= activated && activated <= 0x1a) {
            const u32 bit = 1u << (activated - 0x13);
            if ((state.pending_relation_mask & bit) == 0) {
                state.pending_relation_mask |= bit;
                state.pending_visibility_mask |= bit;
            }
            else {
                state.pending_relation_mask &= ~bit;
                state.pending_visibility_mask &= ~bit;
            }
        }
        else if (activated == 0x23) {
            state.relation_observer_flag = !state.relation_observer_flag;
        }
        else {
            release_gameplay_modal_screen(state, kGameplayModalRelationSlot,
                static_cast<int>(kGameplayModalRelationFlag));
            return false;
        }
        RefreshGameplayRelationMaskDialogControls(state, state.pending_relation_mask,
            state.pending_visibility_mask, state.relation_observer_flag);
    }
}

bool OpenGameplayRelationMaskDialog() {
    return OpenGameplayRelationMaskDialog(gameplay_modal_ui_state());
}

void RefreshGameplayObserverMaskDialogControls(GameplayModalUiState& state, u32 mode,
    u32 mask) {
    state.pending_observer_mode = mode;
    state.pending_observer_mask = mask;
    state.observer_controls.clear();

    UiScreenDefinition& screen = modal_screen(kGameplayModalObserverSlot);
    for (u32 entry = 3; entry <= 6; ++entry) {
        if (entry < screen.entries.size()) {
            set_entry_button_triplet(screen, entry, -1, -1);
        }
    }
    const u32 selected_mode_entry = mode == 4 ? 3 : 3 + mode;
    if (selected_mode_entry < screen.entries.size()) {
        set_entry_button_triplet(screen, selected_mode_entry, 7, 7);
    }

    u32 compact_row = 0;
    for (u32 player = 0; player < kGameplayModalPlayerSlots; ++player) {
        if (player == state.local_player_index || state.players[player].slot_state == 0x14) {
            continue;
        }
        GameplayModalUiControlState control{};
        control.entry_index = 7 + compact_row;
        control.enabled = true;
        control.state = (mask & (1u << player)) != 0 ? 1 : 0;
        control.text = state.players[player].display_name;
        state.observer_controls.push_back(control);

        if (control.entry_index < screen.entries.size()) {
            set_entry_enabled(screen, control.entry_index, true);
            set_entry_button_triplet(screen, control.entry_index,
                control.state ? 6 : 5, control.state ? 5 : 6);
        }
        const u32 text_entry = 0x0e + compact_row;
        if (text_entry < screen.entries.size()) {
            set_entry_enabled(screen, text_entry, true);
            set_entry_text_safe(screen, text_entry, control.text);
        }
        const u32 icon_entry = 0x15 + compact_row;
        if (icon_entry < screen.entries.size()) {
            SetUiScreenEntryI32(screen.entries[icon_entry], 4, 4);
            SetUiScreenEntryI32(screen.entries[icon_entry], 0x3c,
                static_cast<i32>(player + 8));
        }
        ++compact_row;
    }
    for (; compact_row < 7; ++compact_row) {
        set_entry_enabled(screen, 7 + compact_row, false);
        set_entry_enabled(screen, 0x0e + compact_row, false);
        set_entry_enabled(screen, 0x15 + compact_row, false);
    }
}

void RefreshGameplayObserverMaskDialogControls(u32 mode, u32 mask) {
    RefreshGameplayObserverMaskDialogControls(gameplay_modal_ui_state(), mode, mask);
}

bool OpenGameplayObserverMaskDialog(GameplayModalUiState& state) {
    const bool local_is_observer = state.local_player_index < kGameplayModalPlayerSlots &&
        state.players[state.local_player_index].slot_state == 2;
    if (state.scenario_ai_profile_override || !state.generic_ai_profile_mode ||
        local_is_observer) {
        toggle_low_observer_flags(state);
        return false;
    }

    if (!state.observer_mask_active) {
        if (!load_gameplay_modal_screen(state, kGameplayModalObserverSlot,
                selected_faction_record(state, 0x6e),
                static_cast<int>(kGameplayModalObserverFlag))) {
            return false;
        }
        state.pending_observer_mode = state.committed_observer_mode;
        state.pending_observer_mask = state.committed_observer_mask;
        RefreshGameplayObserverMaskDialogControls(state, state.pending_observer_mode,
            state.pending_observer_mask);
    }

    set_cursor_for_gameplay_modal();
    UiScreenDefinition& screen = modal_screen(kGameplayModalObserverSlot);
    while (true) {
        const u32 activated = poll_or_run_gameplay_modal(state, screen);
        if (activated == 0 && state.generic_ai_profile_mode) {
            return false;
        }
        switch (activated) {
        case 0:
            break;
        case 1:
            state.committed_observer_mode = state.pending_observer_mode;
            state.committed_observer_mask = state.pending_observer_mask;
            release_gameplay_modal_screen(state, kGameplayModalObserverSlot,
                static_cast<int>(kGameplayModalObserverFlag));
            return true;
        case 2:
        default:
            release_gameplay_modal_screen(state, kGameplayModalObserverSlot,
                static_cast<int>(kGameplayModalObserverFlag));
            return false;
        case 3:
        case 4:
        case 5:
        case 6:
            state.pending_observer_mode = activated - 3;
            RefreshGameplayObserverMaskDialogControls(state, state.pending_observer_mode,
                state.pending_observer_mask);
            break;
        case 7:
        case 8:
        case 9:
        case 10:
        case 0x0b:
        case 0x0c:
        case 0x0d: {
            const u32 player = observer_toggle_player_index(state, activated - 7);
            state.pending_observer_mask ^= 1u << player;
            RefreshGameplayObserverMaskDialogControls(state, state.pending_observer_mode,
                state.pending_observer_mask);
            break;
        }
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14: {
            const u32 player = observer_toggle_player_index(state, activated - 0x0e);
            state.pending_observer_mask ^= 1u << player;
            RefreshGameplayObserverMaskDialogControls(state, state.pending_observer_mode,
                state.pending_observer_mask);
            break;
        }
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
            break;
        }
    }
}

bool OpenGameplayObserverMaskDialog() {
    return OpenGameplayObserverMaskDialog(gameplay_modal_ui_state());
}

void RefreshGameplayWaitDialogControls(GameplayModalUiState& state) {
    UiScreenDefinition& screen = modal_screen(kGameplayModalWaitSlot);
    state.wait_controls.clear();

    u32 row = 0;
    bool any_completed_wait = false;
    for (u32 player = 0; player < kGameplayModalPlayerSlots && row < 7; ++player) {
        const GameplayModalPlayerSlot& slot = state.players[player];
        if (slot.slot_state == 0x14 || slot.slot_state == 1) {
            continue;
        }
        if (state.wait_required_packet_count != 0 &&
            state.wait_packet_counts[player] >= state.wait_required_packet_count) {
            continue;
        }
        if (state.wait_required_packet_count == 0 && state.wait_threshold_ms != 0 &&
            state.wait_elapsed_ms[player] >= state.wait_threshold_ms) {
            continue;
        }

        const u32 icon_entry = 9 + row;
        const u32 text_entry = 2 + row;
        if (icon_entry < screen.entries.size()) {
            SetUiScreenEntryI32(screen.entries[icon_entry], 4, 4);
            SetUiScreenEntryI32(screen.entries[icon_entry], 0x3c,
                static_cast<i32>(player + 4));
        }
        if (text_entry < screen.entries.size()) {
            char text[160]{};
            const char* name = slot.display_name.empty() ? "Player" : slot.display_name.c_str();
            const char* format = state.wait_remaining_format.empty() ?
                "%s - %d Sec remain." : state.wait_remaining_format.c_str();
            std::snprintf(text, sizeof(text), format, name,
                static_cast<int>(state.wait_elapsed_ms[player] / 1000));
            SetUiScreenEntryI32(screen.entries[text_entry], 4, 0x1000);
            SetUiScreenEntryI32(screen.entries[text_entry], 8, 4);
            SetUiScreenEntryI32(screen.entries[text_entry], 0x0c, 4);
            SetUiScreenEntryI32(screen.entries[text_entry], 0x80, 1);
            set_entry_text_safe(screen, text_entry, text);
        }

        GameplayModalUiControlState control{};
        control.entry_index = text_entry;
        control.flags = player;
        control.text = slot.display_name;
        control.state = static_cast<i32>(state.wait_elapsed_ms[player]);
        state.wait_controls.push_back(control);

        if (state.wait_elapsed_ms[player] == 0) {
            any_completed_wait = true;
        }
        ++row;
    }

    for (; row < 7; ++row) {
        set_entry_enabled(screen, 9 + row, false);
        set_entry_enabled(screen, 2 + row, false);
    }
    if (screen.entries.size() > 1 && any_completed_wait &&
        UiScreenEntryI32(screen.entries[1], 0) == -1) {
        SetUiScreenEntryI32(screen.entries[1], 0, 0);
    }
}

void RefreshGameplayWaitDialogControls() {
    RefreshGameplayWaitDialogControls(gameplay_modal_ui_state());
}

bool OpenGameplayWaitDialog(GameplayModalUiState& state) {
    if (state.wait_dialog_active) {
        return true;
    }
    if (!load_gameplay_modal_screen(state, kGameplayModalWaitSlot,
            selected_faction_record(state, 0x6f),
            static_cast<int>(kGameplayModalWaitFlag))) {
        return false;
    }
    UiScreenDefinition& screen = modal_screen(kGameplayModalWaitSlot);
    // FUN_00430740 disables this button by changing only entry +0.  Clearing
    // entry +4 as the generic helper does removes the imported button flags,
    // so restoring +0 after the timer expires does not recreate the original
    // clickable control.
    if (screen.entries.size() > 1) {
        SetUiScreenEntryI32(screen.entries[1], 0, -1);
    }
    RefreshGameplayWaitDialogControls(state);
    return true;
}

bool OpenGameplayWaitDialog() {
    return OpenGameplayWaitDialog(gameplay_modal_ui_state());
}

bool PollGameplayWaitDialog(GameplayModalUiState& state) {
    if (!state.wait_dialog_active && !OpenGameplayWaitDialog(state)) {
        return false;
    }

    RefreshGameplayWaitDialogControls(state);
    set_cursor_for_gameplay_modal();
    UiScreenDefinition& screen = modal_screen(kGameplayModalWaitSlot);
    int entry_state = 0;
    u32 activated = 0;
    bool result = false;
    if (state.callbacks.poll_modal != nullptr) {
        activated = state.callbacks.poll_modal(state, screen, entry_state);
        result = activated != 0;
    }
    else {
        result = HandleUiScreenInputTick(screen, activated, entry_state);
    }
    state.last_activated_entry = activated;
    state.last_entry_state = entry_state;
    return result;
}

bool PollGameplayWaitDialog() {
    return PollGameplayWaitDialog(gameplay_modal_ui_state());
}

void CloseGameplayWaitDialog(GameplayModalUiState& state) {
    if (state.wait_dialog_active) {
        release_gameplay_modal_screen(state, kGameplayModalWaitSlot,
            static_cast<int>(kGameplayModalWaitFlag));
    }
}

void CloseGameplayWaitDialog() {
    CloseGameplayWaitDialog(gameplay_modal_ui_state());
}

void OpenGameplayPostResultDialog(GameplayModalUiState& state) {
    UiScreenDefinition& screen = modal_screen(kGameplayModalSimpleInfoSlot);
    set_cursor_for_gameplay_modal();
    if (!load_gameplay_modal_screen_from_archive(state, kGameplayModalSimpleInfoSlot,
            kGameplayModalUiArchive, 99, -1, false)) {
        HandleGameplayTrcFatalLoadError(state);
        return;
    }
    run_gameplay_modal(state, screen);
    release_gameplay_modal_screen(state, kGameplayModalSimpleInfoSlot);
    HandleUiScreenDefinitionReleaseWrapper(screen);
}

void OpenGameplayPostResultDialog() {
    OpenGameplayPostResultDialog(gameplay_modal_ui_state());
}

void OpenReplayControlModalDialog(GameplayModalUiState& state) {
    UiScreenDefinition& screen = modal_screen(kGameplayModalSimpleInfoSlot);
    set_cursor_for_gameplay_modal();
    if (!load_gameplay_modal_screen_from_archive(state, kGameplayModalSimpleInfoSlot,
            "JW2_18.TRC", 0x1b, -1, false)) {
        HandleGameplayTrcFatalLoadError(state);
        return;
    }

    while (true) {
        const u32 activated = run_gameplay_modal(state, screen);
        if (activated != 1) {
            break;
        }
#ifdef _WIN32
        HideGameCursor();
        PushBackSurfaceSnapshot();
        HandleDirectDrawFrameBoundary();
#endif
        send_replay_modal_action(state, 0);
#ifdef _WIN32
        HandleDirectDrawFrameBoundary();
        PopBackSurfaceSnapshot();
        ShowGameCursor();
#endif
    }
    release_gameplay_modal_screen(state, kGameplayModalSimpleInfoSlot);
    HandleUiScreenDefinitionReleaseWrapper(screen);
}

void OpenReplayControlModalDialog() {
    OpenReplayControlModalDialog(gameplay_modal_ui_state());
}

void SendReplayModalAction2AndWait(GameplayModalUiState& state) {
#ifdef _WIN32
    HideGameCursor();
    HandleDirectDrawFrameBoundary();
#endif
    send_replay_modal_action(state, 2);
#ifdef _WIN32
    HandleDirectDrawFrameBoundary();
    ShowGameCursor();
#endif
}

void SendReplayModalAction2AndWait() {
    SendReplayModalAction2AndWait(gameplay_modal_ui_state());
}

void OpenGameplayResultTextDialog(GameplayModalUiState& state, const char* text) {
    UiScreenDefinition& screen = modal_screen(kGameplayModalSimpleInfoSlot);
    const std::string message = text != nullptr ? text : state.post_result_text;
    set_cursor_for_gameplay_modal();
    if (!load_gameplay_modal_screen(state, kGameplayModalSimpleInfoSlot, 0x61)) {
        HandleGameplayTrcFatalLoadError(state);
        return;
    }
    if (screen.entries.size() > 2) {
        SetUiScreenEntryI32(screen.entries[2], 8, 2);
        SetUiScreenEntryI32(screen.entries[2], 0x0c, 3);
        SetUiScreenEntryI32(screen.entries[2], 4, 0x1000);
        set_entry_text_safe(screen, 2, message);
    }
    run_gameplay_modal(state, screen);
    release_gameplay_modal_screen(state, kGameplayModalSimpleInfoSlot);
    HandleUiScreenDefinitionReleaseWrapper(screen);
}

void OpenGameplayResultTextDialog(const char* text) {
    OpenGameplayResultTextDialog(gameplay_modal_ui_state(), text);
}

void HandleGameplayTrcFatalLoadError(GameplayModalUiState& state) {
    if (state.generic_ai_profile_mode && state.replay_mode &&
        state.callbacks.publish_corrective_checksum != nullptr) {
        state.callbacks.publish_corrective_checksum(state);
        log_gameplay_modal_action(state,
            GameplayModalUiActionKind::CorrectiveChecksumPublished);
    }
    if (state.callbacks.trc_fatal_error != nullptr) {
        state.callbacks.trc_fatal_error(state);
    }
    log_gameplay_modal_action(state, GameplayModalUiActionKind::MessageShown,
        state.fatal_record_index, 0, state.fatal_archive_name);
}

void HandleGameplayTrcFatalLoadError() {
    HandleGameplayTrcFatalLoadError(gameplay_modal_ui_state());
}

void InitializeUiScreenDefinition(UiScreenDefinition& screen) {
    screen = UiScreenDefinition{};
    screen.selected_index = -1;
    screen.palette_mark = kInvalidUiScreenIndex;
    screen.resource_mark = kInvalidUiScreenIndex;
    screen.sound_mark = kInvalidUiScreenIndex;
}

void HandleUiScreenDefinitionReleaseWrapper(UiScreenDefinition& screen) {
    HandleUiScreenDefinitionResourceRelease(screen);
}

#ifdef _WIN32
bool HandleUiScreenDefinitionFileImport(UiScreenDefinition& screen, const char* path) {
    if (path == nullptr) {
        return false;
    }

    capture_resource_marks(screen);
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::array<u8, 8> header{};
    bool ok = read_exact_file(file, header.data(), static_cast<DWORD>(header.size()));
    if (ok) {
        ok = allocate_entries(screen, read_le_u32(header.data()), read_le_u32(header.data() + 4));
    }
    if (ok && !screen.entries.empty()) {
        ok = read_exact_file(file, screen.entries.data(),
            static_cast<DWORD>(screen.entries.size() * sizeof(UiScreenEntry)));
    }

    u32 active_palette = 0;
    for (u32 i = 0; ok && i < screen.operation_count; ++i) {
        UiScreenOperation operation{};
        ok = read_file_operation(file, operation) &&
            process_file_operation(screen, file, operation, active_palette);
    }

    CloseHandle(file);
    if (!ok) {
        HandleUiScreenDefinitionResourceRelease(screen);
        return false;
    }

    screen.loaded = true;
    return true;
}

bool HandleUiScreenEmbeddedBlobRead(UiScreenDefinition& screen, HANDLE file, u32 byte_count) {
    if (file == INVALID_HANDLE_VALUE || screen.embedded_blob_count >= kUiScreenBlobSlots) {
        return false;
    }

    std::vector<u8> blob(byte_count);
    if (!read_exact_file(file, blob.data(), byte_count)) {
        return false;
    }
    return append_blob(screen, blob.data(), byte_count);
}
#endif

bool HandleUiScreenDefinitionTrcImport(UiScreenDefinition& screen, const char* archive_name,
    u32 record_index) {
    std::vector<u8> record;
    if (!LoadTrcRecordAlloc(archive_name, record_index, record)) {
        return false;
    }

    capture_resource_marks(screen);
    MemoryReader reader(record.data(), record.size());
    std::array<u8, 8> header{};
    if (!reader.read(header.data(), header.size()) ||
        !allocate_entries(screen, read_le_u32(header.data()), read_le_u32(header.data() + 4))) {
        HandleUiScreenDefinitionResourceRelease(screen);
        return false;
    }

    const std::size_t entry_bytes = screen.entries.size() * sizeof(UiScreenEntry);
    if (entry_bytes != 0 && !reader.read(screen.entries.data(), entry_bytes)) {
        HandleUiScreenDefinitionResourceRelease(screen);
        return false;
    }

    bool ok = true;
    u32 active_palette = 0;
    for (u32 i = 0; ok && i < screen.operation_count; ++i) {
        UiScreenOperation operation{};
        ok = read_memory_operation(reader, operation) &&
            process_memory_operation(screen, reader, operation, active_palette);
    }

    if (!ok) {
        HandleUiScreenDefinitionResourceRelease(screen);
        return false;
    }

    screen.loaded = true;
    return true;
}

void HandleUiScreenDefinitionResourceRelease(UiScreenDefinition& screen) {
    if (screen.palette_mark != kInvalidUiScreenIndex) {
        ReleasePaletteCacheSlotsFrom(screen.palette_mark);
    }
    screen.palette_mark = kInvalidUiScreenIndex;

    if (screen.resource_mark != kInvalidUiScreenIndex) {
        ReleaseResourceEntriesFrom(screen.resource_mark);
    }
    screen.resource_mark = kInvalidUiScreenIndex;

#ifdef _WIN32
    if (screen.sound_mark != kInvalidUiScreenIndex) {
        ReleaseDirectSoundBufferSlotsFrom(screen.sound_mark);
    }
#endif
    screen.sound_mark = kInvalidUiScreenIndex;

#ifdef _WIN32
    for (auto& bink : screen.bink_entries) {
        close_bink_entry(bink);
    }
#endif
    screen.bink_entries.clear();
    screen.bink_initialized = false;
    screen.bink_surface_type = 0;

    for (auto& blob : screen.embedded_blobs) {
        blob.clear();
        blob.shrink_to_fit();
    }
    screen.embedded_blob_sizes.fill(0);
    screen.embedded_blob_count = 0;
    screen.entries.clear();
    screen.entry_count = 0;
    screen.operation_count = 0;
    screen.selected_index = -1;
    screen.scroll_tracking = false;
    screen.active_scroll_entry = kInvalidUiScreenIndex;
    screen.last_scroll_tick = 0;
    screen.text_edit_active = false;
    screen.text_edit_entry_index = 0;
    screen.scroll_flags = 0;
    screen.use_custom_text_renderer = false;
    screen.loaded = false;
}

void HandleUiScreenEntriesCentering(UiScreenDefinition& screen, i32 width, i32 height) {
    if (screen.entries.empty()) {
        return;
    }

    const UiScreenEntry& first = screen.entries.front();
    const i32 left = UiScreenEntryI32(first, 0x20);
    const i32 top = UiScreenEntryI32(first, 0x24);
    const i32 right = UiScreenEntryI32(first, 0x28);
    const i32 bottom = UiScreenEntryI32(first, 0x2c);
    const i32 dx = (width / 2 - (right - left) / 2) - left;
    const i32 dy = (height / 2 - (bottom - top) / 2) - top;

    for (auto& entry : screen.entries) {
        SetUiScreenEntryI32(entry, 0x20, UiScreenEntryI32(entry, 0x20) + dx);
        SetUiScreenEntryI32(entry, 0x24, UiScreenEntryI32(entry, 0x24) + dy);
        SetUiScreenEntryI32(entry, 0x28, UiScreenEntryI32(entry, 0x28) + dx);
        SetUiScreenEntryI32(entry, 0x2c, UiScreenEntryI32(entry, 0x2c) + dy);
    }
}

bool HandleUiScreenDefinitionDraw(UiScreenDefinition& screen, HDC dc) {
    bool ok = true;
    screen.skipped_bink_entries = 0;
    screen.skipped_rect_entries = 0;

    for (u32 i = 0; i < screen.entries.size(); ++i) {
        UiScreenEntry& entry = screen.entries[i];
        const u32 flags = static_cast<u32>(UiScreenEntryI32(entry, 0x04));

        if ((flags & 0x0004u) != 0) {
            ok = DrawUiScreenStatusSprite(screen, entry) && ok;
        }
        if ((flags & 0x0100u) != 0) {
            ok = DrawUiScreenScrollBar(screen, entry) && ok;
        }
        if ((flags & 0x10000u) != 0) {
            if (!HandleUiScreenBinkEntryRender(screen, i)) {
                ++screen.skipped_bink_entries;
                ok = false;
            }
        }
        if ((flags & 0x1000u) != 0) {
#ifdef _WIN32
            ok = DrawUiScreenTextEntry(screen, dc, entry) && ok;
#else
            ok = false;
#endif
        }
        if ((flags & 0x0001u) != 0) {
            if (!DrawUiScreenRectangleOutline(entry)) {
                ++screen.skipped_rect_entries;
                ok = false;
            }
        }
    }

    return ok;
}

bool HandleUiScreenInputTick(UiScreenDefinition& screen, u32& activated_entry_index,
    int& entry_state) {
    auto finish_text_edit = [&]() {
        if (!screen.text_edit_active || screen.text_edit_entry_index >= screen.entries.size()) {
            screen.text_edit_active = false;
            return;
        }
        remove_text_cursor_marker(screen.entries[screen.text_edit_entry_index]);
        screen.text_edit_active = false;
    };

    auto begin_text_edit = [&](u32 entry_index) {
        if (entry_index >= screen.entries.size()) {
            return;
        }
        append_text_cursor_marker(screen.entries[entry_index]);
        screen.text_edit_entry_index = entry_index;
        screen.text_edit_active = true;
    };

    UpdatePrimaryMilesMusicPolicy();

    auto handle_text_edit_char = [&](u8 ch) {
        if (!screen.text_edit_active || screen.text_edit_entry_index >= screen.entries.size()) {
            return;
        }
        if (ch == '\t') {
            return;
        }

        UiScreenEntry& entry = screen.entries[screen.text_edit_entry_index];
        char* text = entry_text_mut(entry);
        const std::size_t capacity = entry_text_capacity(entry);
        std::size_t length = std::min<std::size_t>(std::strlen(text), capacity);
        if (capacity < 3) {
            return;
        }

        if (ch == '\b') {
            if (length > 1) {
                text[length - 2] = '_';
                text[length - 1] = '\0';
            }
            return;
        }

        if (length + 1 >= capacity || ch < 0x20) {
            return;
        }
        if (length == 0) {
            text[0] = static_cast<char>(ch);
            text[1] = '_';
            text[2] = '\0';
            return;
        }
        text[length - 1] = static_cast<char>(ch);
        text[length] = '_';
        text[length + 1] = '\0';
    };

    InputState& input = input_state();
    if (!HasQueuedInputEvent()) {
        const i32 mouse_x = static_cast<i32>(input.mouse_x);
        const i32 mouse_y = static_cast<i32>(input.mouse_y);

        for (u32 i = 0; i < screen.entries.size(); ++i) {
            UiScreenEntry& entry = screen.entries[i];
            const i32 state = UiScreenEntryI32(entry, 0);
            if (state == -1 || state == 1) {
                continue;
            }

            if (entry_contains_point(entry, mouse_x, mouse_y)) {
                set_entry_state(entry, 2);
                entry_state = 2;
            }
            else if (state == 2) {
                set_entry_state(entry, 0);
                entry_state = 0;
            }
        }

        if (screen.selected_index >= 0 &&
            static_cast<std::size_t>(screen.selected_index) < screen.entries.size()) {
            UiScreenEntry& selected = screen.entries[static_cast<std::size_t>(screen.selected_index)];
            if (entry_contains_point(selected, mouse_x, mouse_y)) {
                set_entry_state(selected, 1);
                entry_state = 1;
            }
            else {
                set_entry_state(selected, 0);
                entry_state = 0;
            }
        }

        update_active_scroll_tracking(screen, mouse_x, mouse_y);
        sleep_one_millisecond();
        return false;
    }

    InputEvent event{};
    if (!PopInputEvent(event)) {
        sleep_one_millisecond();
        return false;
    }

    if (event.kind == InputEventKind::mouse) {
        if (event.code == 2) {
            activated_entry_index = 0;
            UiScreenEntry* active_entry = nullptr;
            for (u32 i = 0; i < screen.entries.size(); ++i) {
                UiScreenEntry& entry = screen.entries[i];
                if (UiScreenEntryI32(entry, 0) != -1 && entry_contains_point(entry, event.x, event.y)) {
                    activated_entry_index = i;
                    active_entry = &entry;
                    break;
                }
            }

            if (active_entry != nullptr) {
                screen.selected_index = static_cast<i32>(activated_entry_index);
                set_entry_state(*active_entry, 1);
                entry_state = 1;
                if ((static_cast<u32>(UiScreenEntryI32(*active_entry, 0x04)) & 0x100u) != 0) {
                    HandleUiScreenScrollPress(screen, activated_entry_index, event.x, event.y);
                }
                HandleUiScreenStateSound(screen, activated_entry_index);
                HandleUiScreenHoverNoop(*active_entry);
            }
        }
        else if (event.code == 4) {
            finish_text_edit();
            screen.scroll_tracking = false;

            if (screen.selected_index < 0 ||
                static_cast<std::size_t>(screen.selected_index) >= screen.entries.size()) {
                sleep_one_millisecond();
                return false;
            }

            activated_entry_index = static_cast<u32>(screen.selected_index);
            UiScreenEntry& selected = screen.entries[static_cast<std::size_t>(screen.selected_index)];
            SetUiScreenEntryI32(selected, 0x58, 0);
            HandleUiScreenStateSound(screen, activated_entry_index);

            const i32 previous_state = UiScreenEntryI32(selected, 0);
            set_entry_state(selected, 0);
            entry_state = 0;
            screen.selected_index = -1;

            if ((static_cast<u32>(UiScreenEntryI32(selected, 0x04)) & 0x10u) != 0) {
                begin_text_edit(activated_entry_index);
            }
            if (previous_state == 1) {
                return true;
            }
        }
    }
    else if (event.kind == InputEventKind::keyboard) {
        const u32 raw = event.code;
        const u32 key_code = raw & 0xffu;
        const u8 typed_char = static_cast<u8>((raw >> 8) & 0xffu);

        if (screen.text_edit_active && typed_char == 0) {
            sleep_one_millisecond();
            return false;
        }
        if (screen.text_edit_active && typed_char != '\r' && typed_char != 0x1b) {
            handle_text_edit_char(typed_char);
            sleep_one_millisecond();
            return false;
        }

        const u8 accelerator = uppercase_ascii(typed_char);
        activated_entry_index = 0;
        for (u32 i = 0; i < screen.entries.size(); ++i) {
            UiScreenEntry& entry = screen.entries[i];
            const bool accelerator_match = static_cast<u8>(UiScreenEntryI32(entry, 0x10)) ==
                accelerator && accelerator != 0;
            const bool key_match = static_cast<u32>(UiScreenEntryI32(entry, 0x14)) == key_code &&
                key_code != 0;
            if ((accelerator_match || key_match) && UiScreenEntryI32(entry, 0) != -1 &&
                UiScreenEntryI32(entry, 0) != 1) {
                activated_entry_index = i;
                set_entry_state(entry, 1);
                entry_state = 1;
                HandleUiScreenStateSound(screen, activated_entry_index);
                return true;
            }
            if ((accelerator_match || key_match) && UiScreenEntryI32(entry, 0) == 1) {
                set_entry_state(entry, 0);
                entry_state = 0;
            }
        }
    }

    sleep_one_millisecond();
    return false;
}

bool RunUiScreenModalPump(UiScreenDefinition& screen, u32& activated_entry_index,
    int& entry_state) {
    while (!HandleUiScreenInputTick(screen, activated_entry_index, entry_state)) {
#ifdef _WIN32
        // Synchronous gameplay/result modals run after the ordinary frame
        // renderer has unlocked its surface.  sprite_render_state can still
        // contain that previous (now invalid) pointer, so treating a non-null
        // target as writable silently draws the dialog into stale memory.
        // The original modal pump locks the back surface for every draw.
        SpriteRenderTarget target{};
        if (SUCCEEDED(LockBackBufferSpriteRenderTarget(target))) {
            const SpriteRenderTarget previous_target = sprite_render_state().target;
            const bool previous_active = sprite_render_state().active;
            SetSpriteRenderTarget(
                target.pixels, target.width, target.height, target.stride_words);
            HandleUiScreenDefinitionDraw(screen);
            if (previous_active) {
                SetSpriteRenderTarget(previous_target.pixels,
                    previous_target.width, previous_target.height,
                    previous_target.stride_words);
            }
            else {
                ClearSpriteRenderTarget();
            }
            UnlockBackBufferSpriteRenderTarget();
        }
        else {
            HandleUiScreenDefinitionDraw(screen);
        }
        HandleGameCursorPresentation();
#else
        HandleUiScreenDefinitionDraw(screen);
#endif
    }
    return true;
}

bool DrawUiScreenResourceSprite(u32 resource_index, i32 x, i32 y) {
    // UI screen resources use the same zero-token skip RLE stream as gameplay
    // sprites.  Treating the encoded bytes as a flat indexed scanline makes
    // sparse glyphs (notably the victory/defeat outcome sprites) repeat and
    // smear horizontally.  Route them through the canonical sprite decoder so
    // clipping, signed resource offsets, transparency, and RLE skips all match.
    return DrawResourceSpriteNormal(resource_index, x, y);
}

bool DrawUiScreenResourceSprite(
    const UiScreenDefinition& screen, u32 resource_index, i32 x, i32 y) {
    return DrawUiScreenResourceSprite(screen_resource_index(screen, resource_index), x, y);
}

bool HandleUiScreenBinkEntryRender(UiScreenDefinition& screen, u32 entry_index) {
#ifdef _WIN32
    if (entry_index >= screen.entries.size()) {
        return false;
    }
    if (screen.bink_entries.size() < screen.entries.size()) {
        screen.bink_entries.resize(screen.entries.size());
    }
    UiScreenEntry& entry = screen.entries[entry_index];
    UiScreenBinkEntryState& state = screen.bink_entries[entry_index];
    if (!initialize_bink_runtime(screen)) {
        return draw_main_menu_bink_fallback(screen, entry, state);
    }

    if (state.handle == nullptr && !open_bink_entry(screen, entry, state)) {
        return draw_main_menu_bink_fallback(screen, entry, state);
    }

    BinkApi& api = bink_api();
    if (!api.ready()) {
        return draw_main_menu_bink_fallback(screen, entry, state);
    }

    auto* handle = static_cast<BinkHeaderPrefix*>(state.handle);
    if (handle == nullptr) {
        return draw_main_menu_bink_fallback(screen, entry, state);
    }

    PaintMainWindowBlack(RankerMainWindowState().main_window);
    if (api.wait(handle) == 0) {
        api.do_frame(handle);
        if (handle->frame_number == handle->frame_count) {
            if (UiScreenEntryI32(entry, 0x94) == 0) {
                close_bink_entry(state);
                if (!open_bink_entry(screen, entry, state)) {
                    return false;
                }
                handle = static_cast<BinkHeaderPrefix*>(state.handle);
                api.do_frame(handle);
            }
            else {
                api.pause(handle, 1);
                state.paused = true;
            }
        }
        api.next_frame(handle);
    }

    const SpriteRenderTarget& active_target = sprite_render_state().target;
    if (active_target.pixels != nullptr && active_target.width != 0 &&
        active_target.height != 0 && active_target.stride_words != 0) {
        api.copy_to_buffer(handle, active_target.pixels,
            static_cast<i32>(active_target.stride_words * sizeof(u16)), handle->height,
            UiScreenEntryI32(entry, 0x20), UiScreenEntryI32(entry, 0x24),
            screen.bink_surface_type | kBinkCopyDirectDrawFlags);
        return true;
    }

    SpriteRenderTarget target{};
    if (FAILED(LockBackBufferSpriteRenderTarget(target))) {
        return draw_main_menu_bink_fallback(screen, entry, state);
    }
    api.copy_to_buffer(handle, target.pixels,
        static_cast<i32>(target.stride_words * sizeof(u16)), handle->height,
        UiScreenEntryI32(entry, 0x20), UiScreenEntryI32(entry, 0x24),
        screen.bink_surface_type | kBinkCopyDirectDrawFlags);
    UnlockBackBufferSpriteRenderTarget();
    return true;
#else
    (void)screen;
    (void)entry_index;
    return false;
#endif
}

bool RestartUiScreenFlaggedBinkEntries(UiScreenDefinition& screen) {
#ifdef _WIN32
    if (screen.bink_entries.size() < screen.entries.size()) {
        screen.bink_entries.resize(screen.entries.size());
    }
    if (!initialize_bink_runtime(screen)) {
        return false;
    }

    BinkApi& api = bink_api();
    if (!api.ready()) {
        return false;
    }

    bool ok = true;
    for (std::size_t index = 0; index < screen.entries.size(); ++index) {
        UiScreenEntry& entry = screen.entries[index];
        UiScreenBinkEntryState& state = screen.bink_entries[index];
        const u32 flags = static_cast<u32>(UiScreenEntryI32(entry, 4));
        if ((flags & 0x30000u) == 0 || state.handle == nullptr) {
            continue;
        }

        close_bink_entry(state);
        ok = open_bink_entry(screen, entry, state) && ok;

        auto* handle = static_cast<BinkHeaderPrefix*>(state.handle);
        if (handle != nullptr) {
            api.do_frame(handle);
        }
    }
    return ok;
#else
    (void)screen;
    return false;
#endif
}

void PlayJw204BinkMenuScreen(i32 column, i32 row,
    UiScreenModalPumpCallback pump_callback, void* user_data) {
    UiScreenDefinition screen;
    InitializeUiScreenDefinition(screen);
    SetGameCursorIndex(0);

    const u32 record_index = static_cast<u32>(row * 0x14 + column * 0x50);
    if (!HandleUiScreenDefinitionTrcImport(screen, "JW2_04.TRC", record_index)) {
        HandleUiScreenDefinitionReleaseWrapper(screen);
        return;
    }

    SetPrimaryMilesMusicPolicyMode(4);

    u32 activated_entry_index = 0;
    int entry_state = 0;
    auto pump_once = [&]() -> bool {
        if (pump_callback != nullptr) {
            int callback_index = static_cast<int>(activated_entry_index);
            if (!pump_callback(screen, callback_index, entry_state, user_data)) {
                return false;
            }
            activated_entry_index = static_cast<u32>(callback_index);
            return true;
        }
        RunUiScreenModalPump(screen, activated_entry_index, entry_state);
        return true;
    };

    while (pump_once() && activated_entry_index == 2) {
        if (screen.embedded_blob_count == 0) {
            break;
        }
        (void)RestartUiScreenFlaggedBinkEntries(screen);
    }

    HandleUiScreenDefinitionResourceRelease(screen);
    HandleUiScreenDefinitionReleaseWrapper(screen);
}

bool DrawBackBufferRectangleOutline16(i32 left, i32 top, i32 width, i32 height,
    u16 color) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        return draw_rectangle_outline_to_target(target, left, top, width, height, color);
    });
}

bool PutBackBufferPixel16Clipped(i32 x, i32 y, u16 color) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        put_ui_pixel(target, x, y, color);
        return true;
    });
}

bool DrawBackBufferFilledRectangle16(i32 left, i32 top, i32 right, i32 bottom,
    u16 color) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        return draw_filled_rectangle_to_target(target, left, top, right, bottom, color, false);
    });
}

bool DrawBackBufferStippledRectangle16(i32 left, i32 top, i32 right, i32 bottom,
    u16 color) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        return draw_filled_rectangle_to_target(target, left, top, right, bottom, color, true);
    });
}

bool DarkenBackBufferRectangle16(i32 left, i32 top, i32 right, i32 bottom) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        return darken_rectangle_to_target(target, left, top, right, bottom);
    });
}

bool DrawBackBufferLine16(i32 x0, i32 y0, i32 x1, i32 y1, u16 color) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        return draw_line_to_target(target, x0, y0, x1, y1, color);
    });
}

bool OrBackBufferMask32x32(i32 left, i32 top, u16 mask) {
    return draw_with_backbuffer_target([&](const SpriteRenderTarget& target) {
        return or_mask_32x32_to_target(target, left, top, mask);
    });
}

bool OrBackBufferHighRedMask32x32(i32 left, i32 top) {
    // FUN_0050865c ORs the complete DDPIXELFORMAT red channel captured by
    // ConfigureDirectDrawSurfaces at 0x004f4655.  The sprite renderer's
    // `high_red` constant is a deliberately reduced tint mask (0xc000/0x6000)
    // and is not the placement-cell mask used by the original.
    const DirectDrawRuntimeState& draw = direct_draw_state();
    const u16 mask = static_cast<u16>(draw.red_mask != 0
        ? draw.red_mask
        : (draw.pixel_mode_555 != 0 ? 0x7c00u : 0xf800u));
    return OrBackBufferMask32x32(left, top, mask);
}

bool OrBackBufferLowBlueMask32x32(i32 left, i32 top) {
    // FUN_005086d6 likewise uses the complete surface blue channel stored at
    // 0x0144b82c (normally 0x001f), not the 0x0018 sprite-tint shortcut.
    const DirectDrawRuntimeState& draw = direct_draw_state();
    const u16 mask = static_cast<u16>(
        draw.blue_mask != 0 ? draw.blue_mask : 0x001fu);
    return OrBackBufferMask32x32(left, top, mask);
}

bool DrawUiScreenRectangleOutline(const UiScreenEntry& entry, u16 color) {
    const i32 left = UiScreenEntryI32(entry, 0x20);
    const i32 top = UiScreenEntryI32(entry, 0x24);
    return DrawBackBufferRectangleOutline16(left, top, UiScreenEntryI32(entry, 0x28) - left,
        UiScreenEntryI32(entry, 0x2c) - top, color);
}

bool HandleUiScreenStateSound(const UiScreenDefinition& screen, u32 entry_index) {
    if (entry_index >= screen.entries.size() || screen.sound_mark == kInvalidUiScreenIndex) {
        return false;
    }

    const UiScreenEntry& entry = screen.entries[entry_index];
    if ((static_cast<u32>(UiScreenEntryI32(entry, 4)) & 8u) == 0) {
        return false;
    }

    i32 sound_index = -1;
    const i32 state = UiScreenEntryI32(entry, 0);
    if (state == 0) {
        sound_index = UiScreenEntryI32(entry, 0x40);
    }
    else if (state == 1) {
        sound_index = UiScreenEntryI32(entry, 0x44);
    }

    if (sound_index < 0) {
        return false;
    }

#ifdef _WIN32
    return SUCCEEDED(PlayDirectSoundBufferSlot(screen.sound_mark + static_cast<u32>(sound_index),
        0));
#else
    return false;
#endif
}

bool DrawUiScreenStatusSprite(const UiScreenDefinition& screen, const UiScreenEntry& entry) {
    const i32 sprite = selected_state_sprite(entry);
    if (sprite < 0) {
        return false;
    }
    return DrawUiScreenResourceSprite(screen, static_cast<u32>(sprite),
        UiScreenEntryI32(entry, 0x20), UiScreenEntryI32(entry, 0x24));
}

bool DrawUiScreenStatusSprite(const UiScreenEntry& entry) {
    UiScreenDefinition absolute_screen;
    absolute_screen.resource_mark = kInvalidUiScreenIndex;
    return DrawUiScreenStatusSprite(absolute_screen, entry);
}

bool DrawUiScreenScrollBar(const UiScreenDefinition& screen, const UiScreenEntry& entry) {
    const i32 left = UiScreenEntryI32(entry, 0x20);
    const i32 top = UiScreenEntryI32(entry, 0x24);
    const i32 right = UiScreenEntryI32(entry, 0x28);
    const i32 max_value = UiScreenEntryI32(entry, 0x50);
    const i32 current_value = UiScreenEntryI32(entry, 0x54);
    const u32 flags = static_cast<u32>(UiScreenEntryI32(entry, 0x58));

    const u32 track_sprite = static_cast<u32>(UiScreenEntryI32(entry, 0x5c));
    const u32 left_normal = static_cast<u32>(UiScreenEntryI32(entry, 0x60));
    const u32 left_pressed = static_cast<u32>(UiScreenEntryI32(entry, 0x64));
    const u32 right_normal = static_cast<u32>(UiScreenEntryI32(entry, 0x68));
    const u32 right_pressed = static_cast<u32>(UiScreenEntryI32(entry, 0x6c));
    const u32 thumb = static_cast<u32>(UiScreenEntryI32(entry, 0x70));

    const i32 track_start = left + static_cast<i32>(screen_resource_width(screen, left_normal));
    const i32 track_end = right - static_cast<i32>(screen_resource_width(screen, right_normal));
    bool ok = DrawUiScreenResourceSprite(screen, track_sprite, track_start, top);
    ok = DrawUiScreenResourceSprite(screen, (flags & 1u) != 0 ? left_pressed : left_normal,
        left, top) && ok;
    ok = DrawUiScreenResourceSprite(screen, (flags & 2u) != 0 ? right_pressed : right_normal,
        track_end, top) && ok;

    i32 thumb_x = track_start;
    const i32 travel =
        (track_end - track_start) - static_cast<i32>(screen_resource_width(screen, thumb));
    if (max_value > 0) {
        thumb_x += (travel * std::clamp(current_value, 0, max_value - 1)) / max_value;
    }
    ok = DrawUiScreenResourceSprite(screen, thumb, thumb_x, top) && ok;
    return ok;
}

bool DrawUiScreenScrollBar(const UiScreenEntry& entry) {
    UiScreenDefinition absolute_screen;
    absolute_screen.resource_mark = kInvalidUiScreenIndex;
    return DrawUiScreenScrollBar(absolute_screen, entry);
}

bool HandleUiScreenScrollPress(UiScreenDefinition& screen, u32 entry_index,
    i32 mouse_x, i32 mouse_y, u32 tick_ms) {
    if (entry_index >= screen.entries.size()) {
        screen.scroll_tracking = false;
        screen.active_scroll_entry = kInvalidUiScreenIndex;
        return false;
    }

#ifdef _WIN32
    if (tick_ms == 0) {
        tick_ms = timeGetTime();
    }
#endif

    UiScreenEntry& entry = screen.entries[entry_index];
    SetUiScreenEntryI32(entry, 0x58, 0);

    const i32 left = UiScreenEntryI32(entry, 0x20);
    const i32 top = UiScreenEntryI32(entry, 0x24);
    const i32 right = UiScreenEntryI32(entry, 0x28);
    const i32 bottom = UiScreenEntryI32(entry, 0x2c);
    const i32 max_value = UiScreenEntryI32(entry, 0x50);
    const u32 first_button = static_cast<u32>(UiScreenEntryI32(entry, 0x60));
    const u32 second_button = static_cast<u32>(UiScreenEntryI32(entry, 0x68));
    const u32 thumb = static_cast<u32>(UiScreenEntryI32(entry, 0x70));
    const bool vertical = (static_cast<u32>(UiScreenEntryI32(entry, 0x04)) & 0x200u) != 0;

    screen.scroll_tracking = true;
    screen.active_scroll_entry = entry_index;
    screen.last_scroll_tick = tick_ms;

    auto set_scroll_flags = [&](u32 flags) {
        SetUiScreenEntryI32(entry, 0x58, static_cast<i32>(flags));
        screen.scroll_flags = flags;
    };

    if (!vertical) {
        const i32 first_end = left + static_cast<i32>(screen_resource_width(screen, first_button));
        const i32 second_start =
            right - static_cast<i32>(screen_resource_width(screen, second_button));
        if (left <= mouse_x && mouse_x < first_end) {
            set_scroll_flags(1);
            return true;
        }
        if (second_start < mouse_x && mouse_x <= right) {
            set_scroll_flags(2);
            return true;
        }
        if (first_end <= mouse_x && mouse_x < second_start && top < mouse_y && mouse_y < bottom) {
            set_scroll_flags(4);
            const i32 travel = (second_start - first_end) -
                static_cast<i32>(screen_resource_width(screen, thumb));
            const i32 value = travel != 0 ? ((mouse_x - first_end) * max_value) / travel : 0;
            SetUiScreenEntryI32(entry, 0x54, std::clamp(value, 0, std::max(0, max_value - 1)));
            return true;
        }
    }
    else {
        const i32 first_end = top + static_cast<i32>(screen_resource_height(screen, first_button));
        const i32 second_start =
            bottom - static_cast<i32>(screen_resource_height(screen, second_button));
        if (top <= mouse_y && mouse_y < first_end) {
            set_scroll_flags(1);
            return true;
        }
        if (second_start < mouse_y && mouse_y <= bottom) {
            set_scroll_flags(2);
            return true;
        }
        if (first_end <= mouse_y && mouse_y < second_start && left < mouse_x && mouse_x < right) {
            set_scroll_flags(4);
            const i32 travel = (second_start - first_end) -
                static_cast<i32>(screen_resource_height(screen, thumb));
            const i32 value = travel != 0 ? ((mouse_y - first_end) * max_value) / travel : 0;
            SetUiScreenEntryI32(entry, 0x54, std::clamp(value, 0, std::max(0, max_value - 1)));
            return true;
        }
    }

    screen.scroll_tracking = false;
    screen.active_scroll_entry = kInvalidUiScreenIndex;
    return false;
}

void HandleUiScreenHoverNoop(const UiScreenEntry&) {
}

#ifdef _WIN32
bool DrawUiScreenTextEntry(const UiScreenDefinition& screen, HDC dc,
    const UiScreenEntry& entry) {
    const char* text = entry_text(entry);
    if (text[0] == '\0') {
        return true;
    }

    const i32 x = UiScreenEntryI32(entry, 0x20) + entry_i16(entry, 0x18);
    const i32 y = UiScreenEntryI32(entry, 0x24) + entry_i16(entry, 0x1a);
    if (!screen.use_custom_text_renderer) {
        if (dc == nullptr) {
            return false;
        }
        const int length = static_cast<int>(std::strlen(text));
        return TextOutA(dc, x, y, text, length) != FALSE;
    }

    const u8 font_index = static_cast<u8>(UiScreenEntryI32(entry, 0x08));
    const u8 foreground = selected_text_color(entry);
    const u8 background = static_cast<u8>(UiScreenEntryI32(entry, 0x0c));
    SelectTextDrawFont(font_index);
    SelectTextMetricFont(font_index);
    SetTextCursor(x, y, foreground, background);
    return std::strchr(text, '\r') == nullptr ? DrawTextString(text) :
        DrawTextLineUntilCrLf(text);
}
#endif

i32 UiScreenEntryI32(const UiScreenEntry& entry, std::size_t offset) {
    if (offset + sizeof(u32) > entry.bytes.size()) {
        return 0;
    }
    return static_cast<i32>(read_le_u32(entry.bytes.data() + offset));
}

void SetUiScreenEntryI32(UiScreenEntry& entry, std::size_t offset, i32 value) {
    if (offset + sizeof(u32) > entry.bytes.size()) {
        return;
    }
    write_le_i32(entry.bytes.data() + offset, value);
}

}
