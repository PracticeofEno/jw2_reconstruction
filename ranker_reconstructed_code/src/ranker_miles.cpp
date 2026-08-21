#include "ranker_miles.h"

#include "ranker_trc.h"
#include "ranker_win32_compat.h"

#ifdef _WIN32
#include "ranker_cursor.h"
#include "ranker_directx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <digitalv.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ranker {
namespace {

MilesSoundState g_miles_state;
MilesMusicRuntimeState g_music_state;
BriefingBinkMediaState g_briefing_bink_state;
MilesEffectPlaylistState g_effect_playlist_state;
MilesComputedStreamContext g_default_computed_stream_context;

struct AsyncMilesEffectStream {
    std::mutex mutex;
    MilesStreamHandle stream = nullptr;
    std::atomic<bool> active{true};
    bool cancelled = false;
    bool paused = false;
    bool started = false;
};

std::array<std::shared_ptr<AsyncMilesEffectStream>,
    kMilesEffectStreamSlots> g_async_effect_streams;

constexpr std::array<u8, 5> kLegacyMilesNullFallback{'N', 'U', 'L', 'L', 0};

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

void write_le_u32(u8* p, u32 value) {
    p[0] = static_cast<u8>(value & 0xff);
    p[1] = static_cast<u8>((value >> 8) & 0xff);
    p[2] = static_cast<u8>((value >> 16) & 0xff);
    p[3] = static_cast<u8>((value >> 24) & 0xff);
}

std::string fixed_playlist_path(const u8* bytes) {
    const char* begin = reinterpret_cast<const char*>(bytes);
    const char* end = begin + kMilesEffectPlaylistPathBytes;
    const char* nul = static_cast<const char*>(std::memchr(begin, '\0',
        kMilesEffectPlaylistPathBytes));
    return std::string(begin, nul != nullptr ? nul : end);
}

bool add_effect_playlist_entry(MilesEffectEntryKind kind, const char* path,
    u32 record_index) {
    if (kind == MilesEffectEntryKind::Empty) {
        return false;
    }

    try {
        MilesEffectPlaylistEntry entry;
        entry.kind = kind;
        entry.path = path;
        entry.record_index = record_index;
        g_effect_playlist_state.entries.push_back(std::move(entry));
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

void write_effect_playlist_entry(u8* destination,
    const MilesEffectPlaylistEntry& entry) {
    std::memset(destination, 0, kMilesEffectPlaylistRecordBytes);
    write_le_u32(destination, static_cast<u32>(entry.kind));

    std::memcpy(destination + sizeof(u32), entry.path.c_str(), entry.path.size() + 1);

    write_le_u32(destination + sizeof(u32) + kMilesEffectPlaylistPathBytes,
        entry.record_index);
}

std::string fallback_temp_path() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("Jw2Temp" + std::to_string(static_cast<unsigned long long>(ticks)) + ".tmp");
    return path.string();
}

bool load_briefing_bink_source_from_record(BriefingBinkSource& source,
    const char* archive_name, u32 record_index) {
    source.kind = BriefingBinkSourceKind::Empty;

    MilesComputedStreamContext context;
    InitializeMilesTrcArchiveStreamContext(context);

    u32 active_records = 0;
    const bool count_ok = QueryTrcArchiveRecordCount(archive_name, &active_records, nullptr);
    const bool loaded = LoadMilesTrcRecordIntoArchiveContext(context, archive_name, record_index);
    if (count_ok && loaded && active_records > record_index && context.original_size >= 4) {
        if (context.original_size == 4) {
            std::array<u8, sizeof(u32)> pointer{};
            source.kind = BriefingBinkSourceKind::Jw208RecordPointer;
            source.path = "JW2_08.TRC";
            if (ReadMilesTrcArchiveStreamBytes(context, pointer.data(),
                    static_cast<u32>(pointer.size()))) {
                source.record_index = read_le_u32(pointer.data());
            }
        } else {
            source.kind = BriefingBinkSourceKind::ArchiveRecord;
            source.path = archive_name;
            source.record_index = record_index;
        }
    }

    ReleaseMilesTrcArchiveStreamContext(context);
    return true;
}

bool save_briefing_bink_source_to_trc(const BriefingBinkSource& source,
    const char* archive_name, const char* empty_record_name,
    const char* pointer_record_name) {
    switch (source.kind) {
    case BriefingBinkSourceKind::Empty: {
        const u8 empty_payload = 0;
        return HandleTrcMemoryRecordAppend(archive_name, empty_record_name,
            &empty_payload, sizeof(empty_payload), 0x14, 0);
    }
    case BriefingBinkSourceKind::DirectFile:
        return AppendFilePayloadToTrcBuilder(archive_name, source.path.c_str(), 0);
    case BriefingBinkSourceKind::ArchiveRecord:
        return AppendArchiveRecordToTrcBuilder(archive_name, source.path.c_str(),
            source.record_index, 0);
    case BriefingBinkSourceKind::Jw208RecordPointer: {
        std::array<u8, sizeof(u32)> payload{};
        write_le_u32(payload.data(), source.record_index);
        return HandleTrcMemoryRecordAppend(archive_name, pointer_record_name,
            payload.data(), payload.size(), 0x14, 0);
    }
    default:
        return false;
    }
}

bool materialize_briefing_bink_source(BriefingBinkSource& source) {
    if (source.kind != BriefingBinkSourceKind::ArchiveRecord) {
        return true;
    }

    std::string temp_path;
    if (!ExtractBriefingBinkRecordToTempFile(source.path.c_str(), source.record_index,
            temp_path)) {
        return false;
    }

    source.kind = BriefingBinkSourceKind::DirectFile;
    source.path = std::move(temp_path);
    source.record_index = 0;
    return true;
}

bool play_briefing_bink_source(const BriefingBinkSource& source) {
#ifdef _WIN32
    switch (source.kind) {
    case BriefingBinkSourceKind::DirectFile:
        return PlayBinkSource(source.path.c_str(), 0, -1, -1);
    case BriefingBinkSourceKind::ArchiveRecord:
    case BriefingBinkSourceKind::Jw208RecordPointer:
        return PlayBinkTrcRecord(source.path.c_str(), source.record_index, -1, -1);
    case BriefingBinkSourceKind::Empty:
    default:
        return false;
    }
#else
    (void)source;
    return false;
#endif
}

bool close_mci_effect_stream_deferred(MilesStreamHandle stream);
bool close_async_effect_slot(u32 slot, bool nonblocking_backend);
int get_async_effect_slot_status(u32 slot);

void close_effect_slot(u32 slot, bool nonblocking_mci_fallback = false) {
    if (slot >= kMilesEffectStreamSlots) {
        return;
    }

    if (close_async_effect_slot(slot, nonblocking_mci_fallback)) {
        g_effect_playlist_state.streams[slot] = nullptr;
        g_effect_playlist_state.active_entry_indices[slot] =
            kInvalidMilesEffectEntry;
        return;
    }

    MilesStreamHandle& stream = g_effect_playlist_state.streams[slot];
    if (stream != nullptr) {
        const bool released_without_wait = nonblocking_mci_fallback &&
            close_mci_effect_stream_deferred(stream);
        if (!released_without_wait) {
            PauseMilesStream(stream);
            CloseMilesStream(stream);
        }
        stream = nullptr;
    }
    g_effect_playlist_state.active_entry_indices[slot] = kInvalidMilesEffectEntry;
}

u32 acquire_effect_stream_slot() {
    for (u32 i = 0; i < kMilesEffectStreamSlots; ++i) {
        if (g_async_effect_streams[i] == nullptr &&
            g_effect_playlist_state.streams[i] == nullptr) {
            return i;
        }
    }

    // Finished fallback streams still occupy their original five Miles
    // slots.  Reclaim one through the same nonblocking reuse path before
    // resorting to the original random active-stream eviction.
    for (u32 i = 0; i < kMilesEffectStreamSlots; ++i) {
        const int status = g_async_effect_streams[i] != nullptr ?
            get_async_effect_slot_status(i) :
            GetMilesStreamStatus(g_effect_playlist_state.streams[i]);
        if (status == 0) {
            close_effect_slot(i, true);
            return i;
        }
    }

    const u32 slot = static_cast<u32>(std::rand()) % kMilesEffectStreamSlots;
    close_effect_slot(slot, true);
    return slot;
}

u32 current_miles_policy_time_ms() {
#ifdef _WIN32
    return timeGetTime();
#else
    using clock = std::chrono::steady_clock;
    return static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now().time_since_epoch()).count());
#endif
}

void update_original_checksum(MilesComputedStreamContext& context, const u8* data,
    std::size_t size) {
    i16 checksum = static_cast<i16>(context.check_value);
    for (std::size_t i = 0; i < size; ++i) {
        checksum = static_cast<i16>(checksum + static_cast<i8>(data[i]));
    }
    context.check_value = static_cast<u16>(checksum);
}

#ifdef _WIN32
struct MciMilesStream {
    MCIDEVICEID device_id = 0;
    std::string temporary_path;
    int playback_rate = 0xac44;
    int remaining_restarts = 0;
    bool active = false;
    bool paused = false;
};

std::vector<MciMilesStream*> g_mci_streams;
std::mutex g_mci_streams_mutex;

MciMilesStream* find_mci_stream(MilesStreamHandle stream) {
    std::lock_guard<std::mutex> lock(g_mci_streams_mutex);
    const auto found = std::find(g_mci_streams.begin(), g_mci_streams.end(), stream);
    return found != g_mci_streams.end() ? *found : nullptr;
}

void register_mci_stream(MciMilesStream* stream) {
    std::lock_guard<std::mutex> lock(g_mci_streams_mutex);
    g_mci_streams.push_back(stream);
}

void unregister_mci_stream(MciMilesStream* stream) {
    std::lock_guard<std::mutex> lock(g_mci_streams_mutex);
    const auto found = std::find(g_mci_streams.begin(), g_mci_streams.end(), stream);
    if (found != g_mci_streams.end()) {
        g_mci_streams.erase(found);
    }
}

MciMilesStream* last_mci_stream() {
    std::lock_guard<std::mutex> lock(g_mci_streams_mutex);
    return g_mci_streams.empty() ? nullptr : g_mci_streams.back();
}

bool write_mci_memory_stream_file(const char* path, std::string& temporary_path) {
    if (path == nullptr || path[0] != '\\' || path[1] != '\\') {
        return false;
    }

    char* address_end = nullptr;
    const unsigned long long address_value = std::strtoull(path + 2, &address_end, 10);
    if (address_end == path + 2 || address_end == nullptr || *address_end != ',') {
        return false;
    }
    char* size_end = nullptr;
    const unsigned long long size_value = std::strtoull(address_end + 1, &size_end, 10);
    if (size_end == address_end + 1 || size_end == nullptr ||
        std::strcmp(size_end, ".mp3") != 0 || address_value == 0 || size_value == 0 ||
        size_value > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    char temp_directory[MAX_PATH]{};
    if (GetTempPathA(static_cast<DWORD>(std::size(temp_directory)),
            temp_directory) == 0) {
        return false;
    }
    char temp_file[MAX_PATH]{};
    if (GetTempFileNameA(temp_directory, "JwM", 0, temp_file) == 0) {
        return false;
    }

    HANDLE file = CreateFileA(temp_file, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileA(temp_file);
        return false;
    }

    const auto* bytes = reinterpret_cast<const u8*>(
        static_cast<std::uintptr_t>(address_value));
    std::size_t remaining = static_cast<std::size_t>(size_value);
    bool ok = true;
    while (remaining != 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file, bytes, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            break;
        }
        bytes += chunk;
        remaining -= chunk;
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileA(temp_file);
        return false;
    }
    temporary_path = temp_file;
    return true;
}

MciMilesStream* open_mci_stream(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return nullptr;
    }

    std::string temporary_path;
    const char* media_path = path;
    if (path[0] == '\\' && path[1] == '\\') {
        if (!write_mci_memory_stream_file(path, temporary_path)) {
            return nullptr;
        }
        media_path = temporary_path.c_str();
    }

    auto* stream = new (std::nothrow) MciMilesStream;
    if (stream == nullptr) {
        if (!temporary_path.empty()) {
            DeleteFileA(temporary_path.c_str());
        }
        return nullptr;
    }

    MCI_OPEN_PARMSA open{};
    open.lpstrDeviceType = "MPEGVideo";
    open.lpstrElementName = media_path;
    const MCIERROR error = mciSendCommandA(0, MCI_OPEN,
        MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_WAIT,
        reinterpret_cast<DWORD_PTR>(&open));
    if (error != 0) {
        delete stream;
        if (!temporary_path.empty()) {
            DeleteFileA(temporary_path.c_str());
        }
        return nullptr;
    }

    stream->device_id = open.wDeviceID;
    stream->temporary_path = std::move(temporary_path);
    MCI_SET_PARMS time_format{};
    time_format.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
    mciSendCommandA(stream->device_id, MCI_SET,
        MCI_SET_TIME_FORMAT | MCI_WAIT,
        reinterpret_cast<DWORD_PTR>(&time_format));
    register_mci_stream(stream);
    return stream;
}

void close_mci_stream(MciMilesStream* stream) {
    if (stream == nullptr) {
        return;
    }
    unregister_mci_stream(stream);
    // MCI_CLOSE normally stops playback as a side effect, but the MPEGVideo
    // fallback can leave the decoder audible when a paused device is closed
    // from the frontend thread that did not open it.  The reconstruction
    // opens title music on the worker and closes it from the window thread,
    // so issue the explicit stop required to make the transition synchronous
    // before releasing the device and its temporary archive MP3.
    MCI_GENERIC_PARMS close{};
    mciSendCommandA(stream->device_id, MCI_STOP, MCI_WAIT,
        reinterpret_cast<DWORD_PTR>(&close));
    mciSendCommandA(stream->device_id, MCI_CLOSE, MCI_WAIT,
        reinterpret_cast<DWORD_PTR>(&close));
    if (!stream->temporary_path.empty()) {
        DeleteFileA(stream->temporary_path.c_str());
    }
    delete stream;
}

bool close_mci_effect_stream_deferred(MilesStreamHandle handle) {
    MciMilesStream* stream = find_mci_stream(handle);
    if (stream == nullptr) {
        return false;
    }

    // AIL_stream_status is a cheap in-memory query and AIL_close_stream does
    // not stall the scenario script loop.  The 64-bit reconstruction's MCI
    // fallback used two MCI_WAIT commands here, making every Escape dialog
    // skip visibly freeze.  Remove the stream from the live playlist first
    // (so its status is immediately zero), then let the backend release its
    // decoder and temporary archive MP3 away from the gameplay thread.
    unregister_mci_stream(stream);
    try {
        std::thread([stream]() {
            MCI_GENERIC_PARMS command{};
            mciSendCommandA(stream->device_id, MCI_PAUSE, MCI_WAIT,
                reinterpret_cast<DWORD_PTR>(&command));
            mciSendCommandA(stream->device_id, MCI_CLOSE, MCI_WAIT,
                reinterpret_cast<DWORD_PTR>(&command));
            if (!stream->temporary_path.empty()) {
                DeleteFileA(stream->temporary_path.c_str());
            }
            delete stream;
        }).detach();
    }
    catch (...) {
        // Thread creation is exceptionally rare; restoring the registry lets
        // the established synchronous path perform a complete safe close.
        register_mci_stream(stream);
        return false;
    }
    return true;
}

int mci_stream_position(MciMilesStream& stream, DWORD item) {
    MCI_STATUS_PARMS status{};
    status.dwItem = item;
    if (mciSendCommandA(stream.device_id, MCI_STATUS,
            MCI_STATUS_ITEM | MCI_WAIT,
            reinterpret_cast<DWORD_PTR>(&status)) != 0) {
        return -1;
    }
    return static_cast<int>(status.dwReturn);
}

MCIERROR play_mci_stream(MciMilesStream& stream, bool repeat) {
    MCI_PLAY_PARMS play{};
    MCIERROR error = mciSendCommandA(stream.device_id, MCI_PLAY,
        repeat ? MCI_DGV_PLAY_REPEAT : 0,
        reinterpret_cast<DWORD_PTR>(&play));
    // Some MPEGVideo drivers do not advertise MCI_DGV_PLAY_REPEAT.  The
    // periodic service path below also implements infinite repeat, so retry a
    // plain play and let it restart the device at EOF in that case.
    if (error != 0 && repeat) {
        error = mciSendCommandA(stream.device_id, MCI_PLAY, 0,
            reinterpret_cast<DWORD_PTR>(&play));
    }
    return error;
}

bool seek_mci_stream_to_start(MciMilesStream& stream) {
    MCI_SEEK_PARMS seek{};
    return mciSendCommandA(stream.device_id, MCI_SEEK,
        MCI_SEEK_TO_START | MCI_WAIT,
        reinterpret_cast<DWORD_PTR>(&seek)) == 0;
}

struct MilesApi {
    using AIL_set_redist_directory = void (WINAPI*)(const char*);
    using AIL_startup = void (WINAPI*)();
    using AIL_shutdown = void (WINAPI*)();
    using AIL_open_digital_driver = void* (WINAPI*)(int, int, int, int);
    using AIL_close_digital_driver = void (WINAPI*)(void*);
    using AIL_open_stream = void* (WINAPI*)(void*, const char*, int);
    using AIL_close_stream = void (WINAPI*)(void*);
    using AIL_set_stream_loop_count = void (WINAPI*)(void*, int);
    using AIL_start_stream = void (WINAPI*)(void*);
    using AIL_pause_stream = void (WINAPI*)(void*, int);
    using AIL_stream_status = int (WINAPI*)(void*);
    using AIL_stream_volume_levels = void (WINAPI*)(void*, float*, float*);
    using AIL_set_stream_volume_levels = void (WINAPI*)(void*, float, float);
    using AIL_set_preference = int (WINAPI*)(int, int);
    using AIL_serve = void (WINAPI*)();
    using AIL_stream_ms_position = void (WINAPI*)(void*, int*, int*);
    using AIL_set_stream_ms_position = void (WINAPI*)(void*, int);
    using AIL_stream_playback_rate = int (WINAPI*)(void*);
    using AIL_set_stream_playback_rate = void (WINAPI*)(void*, int);

    AIL_set_redist_directory set_redist_directory = nullptr;
    AIL_startup startup = nullptr;
    AIL_shutdown shutdown = nullptr;
    AIL_open_digital_driver open_digital_driver = nullptr;
    AIL_close_digital_driver close_digital_driver = nullptr;
    AIL_open_stream open_stream = nullptr;
    AIL_close_stream close_stream = nullptr;
    AIL_set_stream_loop_count set_stream_loop_count = nullptr;
    AIL_start_stream start_stream = nullptr;
    AIL_pause_stream pause_stream = nullptr;
    AIL_stream_status stream_status = nullptr;
    AIL_stream_volume_levels stream_volume_levels = nullptr;
    AIL_set_stream_volume_levels set_stream_volume_levels = nullptr;
    AIL_set_preference set_preference = nullptr;
    AIL_serve serve = nullptr;
    AIL_stream_ms_position stream_ms_position = nullptr;
    AIL_set_stream_ms_position set_stream_ms_position = nullptr;
    AIL_stream_playback_rate stream_playback_rate = nullptr;
    AIL_set_stream_playback_rate set_stream_playback_rate = nullptr;
};

MilesApi g_miles_api;

template <typename T>
bool resolve_proc(T& target, const char* name) {
    target = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(g_miles_state.module), name));
    return target != nullptr;
}

void clear_miles_api() {
    std::memset(&g_miles_api, 0, sizeof(g_miles_api));
    g_miles_state.api_loaded = false;
}

bool load_miles_module() {
    // The shipped Miles binary is PE32.  The reconstruction is PE32+, so it
    // cannot be loaded into this process; MCI is used as the native fallback.
    if constexpr (sizeof(void*) > sizeof(u32)) {
        return false;
    }
    if (g_miles_state.module != nullptr) {
        return true;
    }

    constexpr std::array<const char*, 6> candidates{
        "mss32.dll",
        "Mss32.dll",
        "ranker\\Mss32.dll",
        "..\\ranker\\Mss32.dll",
        "..\\..\\ranker\\Mss32.dll",
        "..\\..\\..\\ranker\\Mss32.dll",
    };

    for (const char* candidate : candidates) {
        HMODULE module = LoadLibraryA(candidate);
        if (module != nullptr) {
            g_miles_state.module = module;
            return true;
        }
    }
    return false;
}

bool load_miles_api() {
    if (g_miles_state.api_loaded) {
        return true;
    }
    if (!load_miles_module()) {
        return false;
    }

    bool ok = true;
    ok = resolve_proc(g_miles_api.set_redist_directory, "_AIL_set_redist_directory@4") && ok;
    ok = resolve_proc(g_miles_api.startup, "_AIL_startup@0") && ok;
    ok = resolve_proc(g_miles_api.shutdown, "_AIL_shutdown@0") && ok;
    ok = resolve_proc(g_miles_api.open_digital_driver, "_AIL_open_digital_driver@16") && ok;
    ok = resolve_proc(g_miles_api.close_digital_driver, "_AIL_close_digital_driver@4") && ok;
    ok = resolve_proc(g_miles_api.open_stream, "_AIL_open_stream@12") && ok;
    ok = resolve_proc(g_miles_api.close_stream, "_AIL_close_stream@4") && ok;
    ok = resolve_proc(g_miles_api.set_stream_loop_count, "_AIL_set_stream_loop_count@8") && ok;
    ok = resolve_proc(g_miles_api.start_stream, "_AIL_start_stream@4") && ok;
    ok = resolve_proc(g_miles_api.pause_stream, "_AIL_pause_stream@8") && ok;
    ok = resolve_proc(g_miles_api.stream_status, "_AIL_stream_status@4") && ok;
    ok = resolve_proc(g_miles_api.stream_volume_levels, "_AIL_stream_volume_levels@12") && ok;
    ok = resolve_proc(g_miles_api.set_stream_volume_levels,
        "_AIL_set_stream_volume_levels@12") && ok;
    ok = resolve_proc(g_miles_api.set_preference, "_AIL_set_preference@8") && ok;
    ok = resolve_proc(g_miles_api.serve, "_AIL_serve@0") && ok;
    ok = resolve_proc(g_miles_api.stream_ms_position, "_AIL_stream_ms_position@12") && ok;
    ok = resolve_proc(g_miles_api.set_stream_ms_position,
        "_AIL_set_stream_ms_position@8") && ok;
    ok = resolve_proc(g_miles_api.stream_playback_rate, "_AIL_stream_playback_rate@4") && ok;
    ok = resolve_proc(g_miles_api.set_stream_playback_rate,
        "_AIL_set_stream_playback_rate@8") && ok;

    g_miles_state.api_loaded = ok;
    return ok;
}

void unload_miles_module() {
    clear_miles_api();
    if (g_miles_state.module != nullptr) {
        FreeLibrary(static_cast<HMODULE>(g_miles_state.module));
        g_miles_state.module = nullptr;
    }
}
#else
bool close_mci_effect_stream_deferred(MilesStreamHandle) {
    return false;
}

#endif

} // namespace

bool InitMilesSoundSubsystem(const char* redist_directory, int sample_rate,
    int bits_per_sample, int channels) {
#ifdef _WIN32
    ShutdownMilesSoundSubsystem();
    if (!load_miles_api()) {
        unload_miles_module();
        // Modern 64-bit builds cannot load the original 32-bit Mss32.dll.
        // Windows' MPEGVideo MCI driver supplies the stream operations used
        // by the original music policy and replay-side MP3 playback.
        return true;
    }

    g_miles_api.set_redist_directory(redist_directory != nullptr ? redist_directory : ".");
    g_miles_api.startup();
    g_miles_state.digital_driver =
        g_miles_api.open_digital_driver(sample_rate, bits_per_sample, channels, 0);
    if (g_miles_state.digital_driver == nullptr) {
        ShutdownMilesSoundSubsystem();
        return false;
    }

    SetMilesSoundPreference(0x100);
    return true;
#else
    (void)redist_directory;
    (void)sample_rate;
    (void)bits_per_sample;
    (void)channels;
    return false;
#endif
}

void ShutdownMilesSoundSubsystem() {
#ifdef _WIN32
    if (g_miles_state.digital_driver != nullptr && g_miles_api.close_digital_driver != nullptr) {
        g_miles_api.close_digital_driver(g_miles_state.digital_driver);
        g_miles_state.digital_driver = nullptr;
        if (g_miles_api.shutdown != nullptr) {
            g_miles_api.shutdown();
        }
    }
    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        close_effect_slot(slot, true);
    }
    while (MciMilesStream* stream = last_mci_stream()) {
        close_mci_stream(stream);
    }
    g_effect_playlist_state.streams.fill(nullptr);
    g_effect_playlist_state.active_entry_indices.fill(
        kInvalidMilesEffectEntry);
    unload_miles_module();
#endif
}

bool InitMilesMusicRuntime(const char* redist_directory, int sample_rate,
    int bits_per_sample, int channels) {
    if (!g_music_state.enabled) {
        g_music_state.enabled = InitMilesSoundSubsystem(redist_directory, sample_rate,
            bits_per_sample, channels);
    }
    return g_music_state.enabled;
}

void ShutdownMilesMusicRuntime() {
    if (!g_music_state.enabled) {
        return;
    }

    ClosePrimaryMilesMusic();
    CloseSecondaryMilesMusic();
    CloseDirectMilesMusic();
    ShutdownMilesSoundSubsystem();
    g_music_state.enabled = false;
}

void ConstructPrimaryMilesArchiveContext() {
    InitializePrimaryMilesArchiveContext();
}

void DestroyPrimaryMilesArchiveContext() {
    ShutdownPrimaryMilesArchiveContext();
}

void RegisterPrimaryMilesArchiveContextAtExit() {
    std::atexit(DestroyPrimaryMilesArchiveContext);
}

void InitializeAndRegisterPrimaryMilesArchiveContext() {
    ConstructPrimaryMilesArchiveContext();
    RegisterPrimaryMilesArchiveContextAtExit();
}

void ConstructSecondaryMilesArchiveContext() {
    InitializeSecondaryMilesArchiveContext();
}

void DestroySecondaryMilesArchiveContext() {
    ShutdownSecondaryMilesArchiveContext();
}

void RegisterSecondaryMilesArchiveContextAtExit() {
    std::atexit(DestroySecondaryMilesArchiveContext);
}

void InitializeAndRegisterSecondaryMilesArchiveContext() {
    ConstructSecondaryMilesArchiveContext();
    RegisterSecondaryMilesArchiveContextAtExit();
}

bool OpenMilesStream(const char* path, MilesStreamHandle* stream_out) {
#ifdef _WIN32
    if (stream_out != nullptr) {
        *stream_out = nullptr;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        void* stream = g_miles_api.open_stream(g_miles_state.digital_driver, path, 0);
        if (stream_out != nullptr) {
            *stream_out = stream;
        }
        return stream != nullptr;
    }
    MciMilesStream* stream = open_mci_stream(path);
    if (stream_out != nullptr) {
        *stream_out = stream;
    }
    return stream != nullptr;
#else
    (void)path;
    return false;
#endif
}

void InitializeMilesTrcArchiveStreamContext(MilesComputedStreamContext& context) {
    context.payload.clear();
    context.compressed_payload.clear();
    context.archive_name.clear();
    context.computed_path.clear();
    context.directory_entry = {};
    context.record_index = 0;
    context.record_count = 0;
    context.original_size = 0;
    context.stored_size = 0;
    context.read_offset = 0;
    context.check_value = 0;
    context.method = 0;
    context.open = false;
}

void ReleaseMilesTrcArchiveStreamContext(MilesComputedStreamContext& context) {
    context.payload.clear();
    context.compressed_payload.clear();
    context.computed_path.clear();
    context.open = false;
}

void DestroyMilesTrcArchiveStreamContext(MilesComputedStreamContext& context) {
    ReleaseMilesTrcArchiveStreamContext(context);
}

bool OpenMilesTrcRecordDirectoryEntry(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index) {
    context.archive_name = archive_name != nullptr ? archive_name : "";
    context.record_index = record_index;
    ReleaseMilesTrcArchiveStreamContext(context);
    if (archive_name == nullptr) {
        return false;
    }

    TrcDirectoryEntry entry;
    u32 record_count = 0;
    u32 stored_size = 0;
    u32 original_size = 0;
    if (!QueryTrcArchiveRecordCount(archive_name, &record_count, nullptr)) {
        return false;
    }
    context.record_count = record_count;
    if (!QueryTrcRecordSizes(archive_name, record_index, &stored_size, &original_size, &entry)) {
        return false;
    }

    context.directory_entry = std::move(entry);
    context.original_size = original_size;
    context.stored_size = stored_size;
    context.method = context.directory_entry.method;
    context.open = true;
    return true;
}

bool LoadMilesTrcRecordIntoArchiveContext(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index) {
    if (!OpenMilesTrcRecordDirectoryEntry(context, archive_name, record_index)) {
        return false;
    }

    if (context.method != 0 && context.method != 2) {
        context.payload.clear();
        context.read_offset = 0;
        context.check_value = 0;
        context.open = true;
        return true;
    }

    std::vector<u8> payload;
    TrcDirectoryEntry entry;
    if (!LoadTrcRecordAlloc(archive_name, record_index, payload, 0, &entry)) {
        ReleaseMilesTrcArchiveStreamContext(context);
        return false;
    }

    context.directory_entry = std::move(entry);
    context.original_size = static_cast<u32>(payload.size());
    context.stored_size = context.directory_entry.stored_size;
    context.method = context.directory_entry.method;
    context.payload = std::move(payload);
    context.read_offset = 0;
    context.check_value = 0;
    context.open = true;
    return true;
}

bool ReadMilesTrcArchiveStreamBytes(MilesComputedStreamContext& context,
    void* out, u32 byte_count) {
    if (!context.open || (out == nullptr && byte_count != 0)) {
        return false;
    }

    if (context.method == 1) {
        return false;
    }
    if (context.method != 0 && context.method != 2) {
        return true;
    }

    if (context.payload.empty() && context.original_size != 0) {
        if (!LoadMilesTrcRecordIntoArchiveContext(context, context.archive_name.c_str(),
                context.record_index)) {
            return false;
        }
    }

    const std::size_t offset = context.read_offset;
    const std::size_t size = byte_count;
    if (offset > context.payload.size() || size > context.payload.size() - offset) {
        ReleaseMilesTrcArchiveStreamContext(context);
        return false;
    }

    if (size != 0) {
        const u8* source = context.payload.data() + offset;
        std::memcpy(out, source, size);
        if (context.method == 0) {
            update_original_checksum(context, source, size);
        }
    }
    context.read_offset += byte_count;
    return true;
}

u32 QueryMilesTrcRecordOriginalSize(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index) {
    ReleaseMilesTrcArchiveStreamContext(context);
    if (!OpenMilesTrcRecordDirectoryEntry(context, archive_name, record_index)) {
        return kInvalidMilesEffectEntry;
    }

    ReleaseMilesTrcArchiveStreamContext(context);
    return context.original_size;
}

bool QueryMilesTrcRecordStoredAndOriginalSize(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, u32* stored_size, u32* original_size) {
    ReleaseMilesTrcArchiveStreamContext(context);
    if (!OpenMilesTrcRecordDirectoryEntry(context, archive_name, record_index)) {
        return false;
    }

    ReleaseMilesTrcArchiveStreamContext(context);
    if (original_size != nullptr) {
        *original_size = context.original_size;
    }
    if (stored_size != nullptr) {
        *stored_size = context.stored_size;
    }
    return true;
}

bool CopyMilesTrcRecordToBuffer(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, void* out) {
    ReleaseMilesTrcArchiveStreamContext(context);
    if (!LoadMilesTrcRecordIntoArchiveContext(context, archive_name, record_index)) {
        return false;
    }

    const bool ok = ReadMilesTrcArchiveStreamBytes(context, out, context.original_size);
    ReleaseMilesTrcArchiveStreamContext(context);
    return ok;
}

bool LoadMilesTrcRecordAlloc(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, std::vector<u8>& out,
    std::size_t extra_bytes) {
    out.clear();
    ReleaseMilesTrcArchiveStreamContext(context);

    auto return_legacy_null_fallback = [&out, extra_bytes]() {
        if (extra_bytes == 0) {
            return false;
        }
        out.assign(kLegacyMilesNullFallback.begin(), kLegacyMilesNullFallback.end());
        return true;
    };

    if (!LoadMilesTrcRecordIntoArchiveContext(context, archive_name, record_index)) {
        return return_legacy_null_fallback();
    }
    if (extra_bytes > std::numeric_limits<std::size_t>::max() - context.original_size) {
        ReleaseMilesTrcArchiveStreamContext(context);
        return return_legacy_null_fallback();
    }

    const u32 original_size = context.original_size;
    out.assign(static_cast<std::size_t>(original_size) + extra_bytes, 0);
    if (extra_bytes == 1) {
        out[original_size] = 0;
    }
    const bool ok = ReadMilesTrcArchiveStreamBytes(context, out.data(), original_size);
    ReleaseMilesTrcArchiveStreamContext(context);
    if (!ok) {
        return true;
    }
    return true;
}

bool ReadMilesTrcRecordWindowFromOpenArchive(MilesComputedStreamContext& context,
    u32 record_index, u32 unused, u32 byte_count, void* out) {
    (void)unused;
    if (!context.open || context.archive_name.empty() || (out == nullptr && byte_count != 0)) {
        return false;
    }
    if (record_index >= context.record_count) {
        return false;
    }

    std::vector<u8> record;
    if (!LoadTrcRecordAlloc(context.archive_name.c_str(), record_index, record)) {
        ReleaseMilesTrcArchiveStreamContext(context);
        return false;
    }

    // Original 004fdf80 uses the same stack argument for seek offset and read size.
    const std::size_t offset = byte_count;
    const std::size_t size = byte_count;
    if (offset > record.size() || size > record.size() - offset) {
        ReleaseMilesTrcArchiveStreamContext(context);
        return false;
    }

    if (size != 0) {
        std::memcpy(out, record.data() + offset, size);
    }
    return true;
}

bool OpenComputedMilesMp3Stream(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, MilesStreamHandle* stream_out) {
    const u32 original_size = QueryMilesTrcRecordOriginalSize(context, archive_name,
        record_index);
    (void)LoadMilesTrcRecordIntoArchiveContext(context, archive_name, record_index);

    const auto address = reinterpret_cast<std::uintptr_t>(
        context.payload.empty() ? nullptr : context.payload.data());
    context.computed_path = "\\\\" + std::to_string(address) + "," +
        std::to_string(original_size) + ".mp3";

    return OpenMilesStream(context.computed_path.c_str(), stream_out);
}

void CloseMilesStream(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        close_mci_stream(fallback);
        return;
    }
    if (load_miles_api()) {
        g_miles_api.close_stream(stream);
    }
#else
    (void)stream;
#endif
}

void ResetMilesStreamArchiveState(MilesComputedStreamContext& context) {
    ReleaseMilesTrcArchiveStreamContext(context);
}

void ResetMilesStreamArchiveState() {
    ResetMilesStreamArchiveState(g_default_computed_stream_context);
}

void StartMilesStreamWithLoopCount(MilesStreamHandle stream, int loop_count) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        // Miles treats zero as infinite and a positive value as the total
        // number of plays.  Native MCI repeat covers the infinite case; the
        // service path handles finite counts and drivers without repeat.
        fallback->remaining_restarts = loop_count <= 0 ? -1 : loop_count - 1;
        fallback->paused = false;
        fallback->active = play_mci_stream(*fallback,
            fallback->remaining_restarts < 0) == 0;
        return;
    }
    if (load_miles_api()) {
        g_miles_api.set_stream_loop_count(stream, loop_count);
        g_miles_api.start_stream(stream);
    }
#else
    (void)stream;
    (void)loop_count;
#endif
}

void PauseMilesStream(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        MCI_GENERIC_PARMS pause{};
        if (mciSendCommandA(fallback->device_id, MCI_PAUSE, MCI_WAIT,
                reinterpret_cast<DWORD_PTR>(&pause)) == 0) {
            fallback->paused = true;
        }
        return;
    }
    if (load_miles_api()) {
        g_miles_api.pause_stream(stream, 1);
    }
#else
    (void)stream;
#endif
}

void StopMilesStream(MilesStreamHandle stream) {
    PauseMilesStream(stream);
}

void ResumeMilesStream(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        MCI_GENERIC_PARMS resume{};
        MCIERROR error = mciSendCommandA(fallback->device_id, MCI_RESUME, 0,
            reinterpret_cast<DWORD_PTR>(&resume));
        if (error != 0) {
            error = play_mci_stream(*fallback,
                fallback->remaining_restarts < 0);
        }
        fallback->paused = error != 0;
        fallback->active = error == 0;
        return;
    }
    if (load_miles_api()) {
        g_miles_api.pause_stream(stream, 0);
    }
#else
    (void)stream;
#endif
}

int GetMilesStreamStatus(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        // Miles exposes stream status from its serviced state.  Querying MCI
        // synchronously for every script cue/frame put decoder IPC directly
        // on the simulation input path. ServeMilesSound refreshes `active`
        // at the same 100 ms policy cadence used for all fallback streams.
        return fallback->active ? 1 : 0;
    }
    if (load_miles_api()) {
        g_miles_state.last_status = g_miles_api.stream_status(stream);
        return g_miles_state.last_status;
    }
#else
    (void)stream;
#endif
    return 0;
}

int GetMilesStreamVolume(MilesStreamHandle stream) {
#ifdef _WIN32
    if (find_mci_stream(stream) != nullptr) {
        return g_miles_state.last_volume_percent;
    }
    if (load_miles_api()) {
        float left = 0.0f;
        float right = 0.0f;
        g_miles_api.stream_volume_levels(stream, &left, &right);
        g_miles_state.last_volume_percent = static_cast<int>((left + right) * 50.0f);
        return g_miles_state.last_volume_percent;
    }
#else
    (void)stream;
#endif
    return 0;
}

void SetMilesStreamVolume(MilesStreamHandle stream, int volume_percent) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        volume_percent = std::clamp(volume_percent, 0, 100);
        MCI_DGV_SETAUDIO_PARMS volume{};
        volume.dwItem = MCI_DGV_SETAUDIO_VOLUME;
        volume.dwValue = static_cast<DWORD>(volume_percent * 10);
        mciSendCommandA(fallback->device_id, MCI_SETAUDIO,
            MCI_DGV_SETAUDIO_ITEM | MCI_DGV_SETAUDIO_VALUE | MCI_WAIT,
            reinterpret_cast<DWORD_PTR>(&volume));
        g_miles_state.last_volume_percent = volume_percent;
        return;
    }
    if (load_miles_api()) {
        const float level = static_cast<float>(volume_percent) / 100.0f;
        g_miles_api.set_stream_volume_levels(stream, level, level);
    }
#else
    (void)stream;
    (void)volume_percent;
#endif
}

void SetMilesSoundPreference(int value) {
#ifdef _WIN32
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        const int scaled = (value + ((value >> 31) & 7)) >> 3;
        g_miles_api.set_preference(0x2a, scaled);
    }
#else
    (void)value;
#endif
}

void ServeMilesSound() {
#ifdef _WIN32
    const u32 now = timeGetTime();
    if (now - g_miles_state.last_serve_time <= 99) {
        return;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        g_miles_api.serve();
    }
    std::lock_guard<std::mutex> lock(g_mci_streams_mutex);
    for (MciMilesStream* stream : g_mci_streams) {
        if (stream == nullptr || !stream->active || stream->paused) {
            continue;
        }
        MCI_STATUS_PARMS status{};
        status.dwItem = MCI_STATUS_MODE;
        if (mciSendCommandA(stream->device_id, MCI_STATUS,
                MCI_STATUS_ITEM | MCI_WAIT,
                reinterpret_cast<DWORD_PTR>(&status)) != 0 ||
            status.dwReturn != MCI_MODE_STOP) {
            continue;
        }
        if (stream->remaining_restarts == 0) {
            stream->active = false;
            continue;
        }
        if (stream->remaining_restarts > 0) {
            --stream->remaining_restarts;
        }
        stream->active = seek_mci_stream_to_start(*stream) &&
            play_mci_stream(*stream, stream->remaining_restarts < 0) == 0;
    }
    g_miles_state.last_serve_time = now;
#endif
}

int GetMilesStreamCurrentMs(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        g_miles_state.last_current_ms =
            mci_stream_position(*fallback, MCI_STATUS_POSITION);
        return g_miles_state.last_current_ms;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        int current = 0;
        g_miles_api.stream_ms_position(stream, &current, nullptr);
        g_miles_state.last_current_ms = current;
        return current;
    }
#else
    (void)stream;
#endif
    return -1;
}

int GetMilesStreamLengthMs(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        g_miles_state.last_length_ms =
            mci_stream_position(*fallback, MCI_STATUS_LENGTH);
        return g_miles_state.last_length_ms;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        int length = 0;
        g_miles_api.stream_ms_position(stream, nullptr, &length);
        g_miles_state.last_length_ms = length;
        return length;
    }
#else
    (void)stream;
#endif
    return -1;
}

void SetMilesStreamMsPosition(MilesStreamHandle stream, int position_ms) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        MCI_SEEK_PARMS seek{};
        seek.dwTo = static_cast<DWORD>(std::max(position_ms, 0));
        mciSendCommandA(fallback->device_id, MCI_SEEK,
            MCI_TO | MCI_WAIT, reinterpret_cast<DWORD_PTR>(&seek));
        return;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        g_miles_api.set_stream_ms_position(stream, position_ms);
    }
#else
    (void)stream;
    (void)position_ms;
#endif
}

int GetMilesStreamPlaybackRate(MilesStreamHandle stream) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        return fallback->playback_rate;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        g_miles_state.last_playback_rate = g_miles_api.stream_playback_rate(stream);
        return g_miles_state.last_playback_rate;
    }
#else
    (void)stream;
#endif
    return -1;
}

void SetMilesStreamPlaybackRate(MilesStreamHandle stream, int playback_rate) {
#ifdef _WIN32
    if (MciMilesStream* fallback = find_mci_stream(stream)) {
        if (playback_rate <= 0) {
            return;
        }
        MCI_DGV_SET_PARMS speed{};
        speed.dwSpeed = static_cast<DWORD>(std::clamp<std::uint64_t>(
            (static_cast<std::uint64_t>(playback_rate) * 1000u) / 0xac44u,
            1u, std::numeric_limits<DWORD>::max()));
        if (mciSendCommandA(fallback->device_id, MCI_SET,
                MCI_DGV_SET_SPEED | MCI_WAIT,
                reinterpret_cast<DWORD_PTR>(&speed)) == 0) {
            fallback->playback_rate = playback_rate;
        }
        return;
    }
    if (g_miles_state.digital_driver != nullptr && load_miles_api()) {
        g_miles_api.set_stream_playback_rate(stream, playback_rate);
    }
#else
    (void)stream;
    (void)playback_rate;
#endif
}

void InitializePrimaryMilesMusicPolicy() {
    g_music_state.primary_policy_mode = 0;
    g_music_state.primary_policy_record = kPrimaryMilesMusicStoppedRecord;
    g_music_state.primary_policy_last_tick = current_miles_policy_time_ms();
    InitMilesMusicRuntime();
}

void ShutdownPrimaryMilesMusicPolicy() {
    ShutdownMilesMusicRuntime();
}

void SetPrimaryMilesMusicPolicyMode(u32 mode) {
    // A completed gameplay flow shuts the Miles runtime down before the
    // frontend is shown again.  The title immediately selects an active
    // policy (mode 2), so restore the runtime here before that policy tries
    // to open its stream.  Mode 0/1 remain shutdown-safe and must not revive
    // audio while a frontend is being left or the process is closing.
    if (mode >= 2u && !g_music_state.enabled) {
        InitMilesMusicRuntime();
    }
    g_music_state.primary_policy_mode = mode;
    g_music_state.primary_policy_last_tick = current_miles_policy_time_ms() - 2000u;
    UpdatePrimaryMilesMusicPolicy();
}

void UpdatePrimaryMilesMusicPolicy() {
    const u32 now = current_miles_policy_time_ms();
    if (now - g_music_state.primary_policy_last_tick <= 1999u) {
        return;
    }

    g_music_state.primary_policy_last_tick = now;

    const auto primary_is_playing = []() {
        return (GetPrimaryMilesMusicStatus() & 0xff) != 0;
    };
    const auto play_primary_record = [](u32 record_index) {
        g_music_state.primary_policy_record = record_index;
        PlayPrimaryMilesMusicRecord(record_index);
    };
    const auto current_faction_id = []() {
        return g_music_state.primary_policy_faction_index;
    };

    switch (g_music_state.primary_policy_mode) {
    case 0:
        break;
    case 1:
        // AIL_stream_status is only a policy hint.  The 64-bit MCI fallback
        // can report a stale stopped state while its decoder is still owned
        // by the previous frontend.  Mode 1 means "no primary stream" in the
        // original transition, so close an existing handle unconditionally.
        // Otherwise returning to mode 2 can open a second title track while
        // the orphaned first device remains audible.
        if (g_music_state.primary_stream != nullptr) {
            ClosePrimaryMilesMusic();
        }
        break;
    case 2:
        if (g_music_state.primary_policy_raw_volume != 0 &&
            ((g_music_state.primary_policy_record != 0 &&
                 g_music_state.primary_policy_record != 1) ||
                !primary_is_playing())) {
            play_primary_record(static_cast<u32>(std::rand()) & 1u);
        }
        break;
    case 3:
        if (g_music_state.primary_policy_raw_volume != 0) {
            const u32 record = current_faction_id() * 3u + 2u;
            if ((g_music_state.primary_policy_record != record &&
                    g_music_state.primary_policy_record != record + 1u) ||
                !primary_is_playing()) {
                play_primary_record(record + (static_cast<u32>(std::rand()) & 1u));
            }
        }
        break;
    case 4:
        if (g_music_state.primary_policy_raw_volume != 0) {
            const u32 record = current_faction_id() + 0x0eu;
            if (g_music_state.primary_policy_record != record || !primary_is_playing()) {
                play_primary_record(record);
            }
        }
        break;
    case 5:
        if (g_music_state.primary_policy_raw_volume != 0) {
            const u32 record = current_faction_id() * 3u + 4u;
            if (g_music_state.primary_policy_record != record || !primary_is_playing()) {
                play_primary_record(record);
            }
        }
        break;
    case 6:
        if (g_music_state.primary_policy_raw_volume != 0 &&
            (g_music_state.primary_policy_record != 0x12u || !primary_is_playing())) {
            play_primary_record(0x12u);
        }
        break;
    default:
        break;
    }
}

void StopPrimaryMilesMusicPolicy() {
    StopPrimaryMilesMusic();
}

void ResumePrimaryMilesMusicPolicy() {
    ResumePrimaryMilesMusic();
}

void ApplyPrimaryMilesMusicPolicyVolume() {
    ApplyPrimaryMilesMusicVolume(g_music_state.primary_policy_raw_volume);
}

void PausePrimaryMusicFromPolicy() {
    StopPrimaryMilesMusicPolicy();
}

void HandlePrimaryMusicPolicyResume() {
    ResumePrimaryMilesMusicPolicy();
}

void HandlePrimaryMusicPolicyVolumeApply() {
    ApplyPrimaryMilesMusicPolicyVolume();
}

void SetPrimaryMilesMusicPolicyRawVolume(u32 raw_volume) {
    g_music_state.primary_policy_raw_volume = raw_volume;
}

void SetPrimaryMilesMusicPolicyFactionIndex(u32 faction_index) {
    g_music_state.primary_policy_faction_index = faction_index;
}

void SetPrimaryMilesMusicPolicyFactionBase(u32 faction_index, u32 record_base) {
    if (faction_index < g_music_state.primary_policy_faction_music_bases.size()) {
        g_music_state.primary_policy_faction_music_bases[faction_index] = record_base;
    }
}

void PlayPrimaryMilesMusicRecord(u32 record_index, const char* archive_name) {
    if (!g_music_state.enabled) {
        return;
    }

    ClosePrimaryMilesMusic();
    if (!OpenComputedMilesMp3Stream(g_music_state.primary_context, archive_name,
            record_index, &g_music_state.primary_stream)) {
        return;
    }
    StartMilesStreamWithLoopCount(g_music_state.primary_stream, 1);
}

void ClosePrimaryMilesMusic() {
    if (g_music_state.enabled && g_music_state.primary_stream != nullptr) {
        PauseMilesStream(g_music_state.primary_stream);
        CloseMilesStream(g_music_state.primary_stream);
        g_music_state.primary_stream = nullptr;
    }
    ResetMilesStreamArchiveState(g_music_state.primary_context);
}

void StopPrimaryMilesMusic() {
    if (g_music_state.enabled && g_music_state.primary_stream != nullptr) {
        StopMilesStream(g_music_state.primary_stream);
    }
}

void ResumePrimaryMilesMusic() {
    if (g_music_state.enabled && g_music_state.primary_stream != nullptr) {
        ResumeMilesStream(g_music_state.primary_stream);
    }
}

int GetPrimaryMilesMusicStatus() {
    if (g_music_state.enabled && g_music_state.primary_stream != nullptr) {
        return GetMilesStreamStatus(g_music_state.primary_stream);
    }
    return 0;
}

void ApplyPrimaryMilesMusicVolume(u32 raw_volume) {
    if (g_music_state.enabled && g_music_state.primary_stream != nullptr) {
        SetMilesStreamVolume(g_music_state.primary_stream,
            static_cast<int>((raw_volume * 100u) / 0xffffu));
    }
}

void PlaySecondaryMilesMusicRecord(u32 record_index, const char* archive_name) {
    CloseSecondaryMilesMusic();
    if (!OpenComputedMilesMp3Stream(g_music_state.secondary_context, archive_name,
            record_index, &g_music_state.secondary_stream)) {
        return;
    }
    StartMilesStreamWithLoopCount(g_music_state.secondary_stream, 1);
}

void CloseSecondaryMilesMusic() {
    if (g_music_state.secondary_stream != nullptr) {
        PauseMilesStream(g_music_state.secondary_stream);
        CloseMilesStream(g_music_state.secondary_stream);
        g_music_state.secondary_stream = nullptr;
    }
    ResetMilesStreamArchiveState(g_music_state.secondary_context);
}

bool PlayDirectMilesMusic(const char* path) {
    CloseDirectMilesMusic();
    if (!OpenMilesStream(path, &g_music_state.direct_stream)) {
        return false;
    }
    StartMilesStreamWithLoopCount(g_music_state.direct_stream, 1);
    return true;
}

void CloseDirectMilesMusic() {
    if (g_music_state.direct_stream != nullptr) {
        PauseMilesStream(g_music_state.direct_stream);
        CloseMilesStream(g_music_state.direct_stream);
        g_music_state.direct_stream = nullptr;
    }
}

void StopDirectMilesMusic() {
    if (g_music_state.direct_stream != nullptr) {
        StopMilesStream(g_music_state.direct_stream);
    }
}

void ResumeDirectMilesMusic() {
    if (g_music_state.direct_stream != nullptr) {
        ResumeMilesStream(g_music_state.direct_stream);
    }
}

int GetDirectMilesMusicStatus() {
    return g_music_state.direct_stream != nullptr ?
        GetMilesStreamStatus(g_music_state.direct_stream) : 0;
}

int GetDirectMilesMusicVolume() {
    return g_music_state.direct_stream != nullptr ?
        GetMilesStreamVolume(g_music_state.direct_stream) : 0;
}

void SetDirectMilesMusicVolume(int volume_percent) {
    if (g_music_state.direct_stream != nullptr) {
        SetMilesStreamVolume(g_music_state.direct_stream, volume_percent);
    }
}

int GetDirectMilesMusicCurrentMs() {
    return g_music_state.direct_stream != nullptr ?
        GetMilesStreamCurrentMs(g_music_state.direct_stream) : 0;
}

int GetDirectMilesMusicLengthMs() {
    return g_music_state.direct_stream != nullptr ?
        GetMilesStreamLengthMs(g_music_state.direct_stream) : 0;
}

void SetDirectMilesMusicMsPosition(int position_ms) {
    if (g_music_state.direct_stream != nullptr) {
        SetMilesStreamMsPosition(g_music_state.direct_stream, position_ms);
    }
}

int GetDirectMilesMusicPlaybackRate() {
    return g_music_state.direct_stream != nullptr ?
        GetMilesStreamPlaybackRate(g_music_state.direct_stream) : 0;
}

void SetDirectMilesMusicPlaybackRate(int playback_rate) {
    if (g_music_state.direct_stream != nullptr) {
        SetMilesStreamPlaybackRate(g_music_state.direct_stream, playback_rate);
    }
}

void InitializeBriefingBinkMediaState() {
    g_briefing_bink_state.archive_name.clear();
    ResetBriefingStartBinkSource();
    ResetBriefingEndBinkSource();
    ResetMilesEffectPlaylist();
}

void SetBriefingBinkArchiveName(const char* archive_name) {
    g_briefing_bink_state.archive_name = archive_name;
}

int CompareBriefingBinkArchiveName(const char* lhs, const char* rhs) {
#ifdef _WIN32
    return CompareMbcsCaseInsensitive(lhs, rhs);
#else
    return std::strcmp(lhs, rhs);
#endif
}

bool ExtractBriefingBinkRecordToTempFile(const char* archive_name, u32 record_index,
    std::string& temp_path) {
#ifdef _WIN32
    char temp_directory[kMilesEffectPlaylistPathBytes]{};
    char temp_file[kMilesEffectPlaylistPathBytes]{};
    if (GetTempPathA(kMilesEffectPlaylistPathBytes, temp_directory) == 0 ||
        GetTempFileNameA(temp_directory, "Jw2Temp", 0, temp_file) == 0) {
        return false;
    }
    temp_path = temp_file;
#else
    temp_path = fallback_temp_path();
#endif

    if (!ExtractTrcRecordToFile(archive_name, record_index, temp_path.c_str())) {
#ifdef _WIN32
        DeleteFileA(temp_path.c_str());
#else
        std::error_code ignored;
        std::filesystem::remove(temp_path, ignored);
#endif
        temp_path.clear();
        return false;
    }
    return true;
}

bool MaterializeBriefingBinkSourcesForArchive(const char* archive_name) {
    if (CompareBriefingBinkArchiveName(archive_name,
            g_briefing_bink_state.archive_name.c_str()) != 0) {
        return true;
    }

    if (!materialize_briefing_bink_source(g_briefing_bink_state.start) ||
        !materialize_briefing_bink_source(g_briefing_bink_state.end)) {
        return false;
    }

    for (MilesEffectPlaylistEntry& entry : g_effect_playlist_state.entries) {
        if (entry.kind != MilesEffectEntryKind::ArchiveRecord) {
            return false;
        }
        std::string temp_path;
        if (!ExtractBriefingBinkRecordToTempFile(entry.path.c_str(), entry.record_index,
                temp_path)) {
            return false;
        }
        entry.kind = MilesEffectEntryKind::DirectFile;
        entry.path = std::move(temp_path);
        entry.record_index = 0;
    }
    return true;
}

void ResetBriefingStartBinkSource() {
    g_briefing_bink_state.start = BriefingBinkSource{};
}

bool ResolveBriefingStartBinkSourceRecord(const char* archive_name) {
    return LoadBriefingStartBinkSourceFromTrc(archive_name);
}

bool LoadBriefingStartBinkSourceFromTrc(const char* archive_name) {
    return load_briefing_bink_source_from_record(g_briefing_bink_state.start,
        archive_name, kBriefingStartBinkRecord);
}

bool SaveBriefingStartBinkSourceToTrc(const char* archive_name) {
    return save_briefing_bink_source_to_trc(g_briefing_bink_state.start, archive_name,
        "BRIFBIK_SRT", "BRIFBIK_SRT");
}

bool HandleBriefingStartVideoPlayback() {
    return PlayBriefingStartBinkSource();
}

bool PlayBriefingStartBinkSource() {
#ifdef _WIN32
    HideGameCursor();
    HandleDirectDrawFrameBoundary();
    const bool played = play_briefing_bink_source(g_briefing_bink_state.start);
    HandleDirectDrawFrameBoundary();
    ShowGameCursor();
    return played;
#else
    return false;
#endif
}

void ResetBriefingEndBinkSource() {
    g_briefing_bink_state.end = BriefingBinkSource{};
}

bool ResolveBriefingEndBinkSourceRecord(const char* archive_name) {
    return LoadBriefingEndBinkSourceFromTrc(archive_name);
}

bool LoadBriefingEndBinkSourceFromTrc(const char* archive_name) {
    return load_briefing_bink_source_from_record(g_briefing_bink_state.end,
        archive_name, kBriefingEndBinkRecord);
}

bool SaveBriefingEndBinkSourceToTrc(const char* archive_name) {
    return save_briefing_bink_source_to_trc(g_briefing_bink_state.end, archive_name,
        "BRIFBIK_END", "BRIFBIK_SRT");
}

bool HandleBriefingEndVideoPlayback() {
    return PlayBriefingEndBinkSource();
}

bool PlayBriefingEndBinkSource() {
#ifdef _WIN32
    HideGameCursor();
    HandleDirectDrawFrameBoundary();
    if (g_briefing_bink_state.active_game_mode == 7) {
        StopPrimaryMilesMusicPolicy();
    }
    const bool played = play_briefing_bink_source(g_briefing_bink_state.end);
    if (g_briefing_bink_state.active_game_mode == 7) {
        ResumePrimaryMilesMusicPolicy();
    }
    HandleDirectDrawFrameBoundary();
    ShowGameCursor();
    return played;
#else
    return false;
#endif
}

void SetBriefingBinkActiveGameMode(u32 mode) {
    g_briefing_bink_state.active_game_mode = mode;
}

bool InitializeMilesEffectSoundSubsystem() {
    return InitMilesSoundSubsystem(".", 0xac44, 0x10, 2);
}

void HandleMilesEffectSoundShutdown() {
    ShutdownMilesEffectSoundSubsystem();
}

void ShutdownMilesEffectSoundSubsystem() {
    ShutdownMilesSoundSubsystem();
}

void ResetMilesEffectPlaylist() {
    CloseAllMilesEffectPlaylistStreams();
    std::vector<MilesEffectPlaylistEntry>().swap(g_effect_playlist_state.entries);
}

namespace {

bool open_miles_effect_entry_stream(const MilesEffectPlaylistEntry& entry,
    MilesComputedStreamContext& context, MilesStreamHandle* stream) {
    switch (entry.kind) {
    case MilesEffectEntryKind::DirectFile:
        return OpenMilesStream(entry.path.c_str(), stream);
    case MilesEffectEntryKind::ArchiveRecord:
        return OpenComputedMilesMp3Stream(context, entry.path.c_str(),
            entry.record_index, stream);
    case MilesEffectEntryKind::Jw204Record:
        return OpenComputedMilesMp3Stream(context, "JW2_04.TRC",
            entry.record_index, stream);
    case MilesEffectEntryKind::Empty:
    default:
        return false;
    }
}

bool should_use_async_mci_effect_backend() {
#ifdef _WIN32
    return !load_miles_api();
#else
    return false;
#endif
}

bool start_async_mci_effect_stream(u32 slot, i32 entry_index,
    const MilesEffectPlaylistEntry& entry) {
#ifdef _WIN32
    if (slot >= kMilesEffectStreamSlots) {
        return false;
    }
    std::shared_ptr<AsyncMilesEffectStream> state;
    try {
        state = std::make_shared<AsyncMilesEffectStream>();
        std::thread([state, entry]() {
            MilesComputedStreamContext context{};
            InitializeMilesTrcArchiveStreamContext(context);
            MilesStreamHandle stream = nullptr;
            const bool opened = open_miles_effect_entry_stream(
                entry, context, &stream);
            ReleaseMilesTrcArchiveStreamContext(context);
            if (!opened || stream == nullptr) {
                state->active.store(false);
                return;
            }

            bool discard = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->cancelled) {
                    discard = true;
                }
                else {
                    state->stream = stream;
                    if (!state->paused) {
                        StartMilesStreamWithLoopCount(stream, 1);
                        state->started = true;
                    }
                }
            }
            if (discard) {
                CloseMilesStream(stream);
            }
        }).detach();
    }
    catch (...) {
        return false;
    }

    g_async_effect_streams[slot] = state;
    g_effect_playlist_state.streams[slot] = state.get();
    g_effect_playlist_state.active_entry_indices[slot] =
        static_cast<u32>(entry_index);
    return true;
#else
    (void)slot;
    (void)entry_index;
    (void)entry;
    return false;
#endif
}

bool close_async_effect_slot(u32 slot, bool nonblocking_backend) {
    if (slot >= kMilesEffectStreamSlots ||
        g_async_effect_streams[slot] == nullptr) {
        return false;
    }

    const std::shared_ptr<AsyncMilesEffectStream> state =
        std::move(g_async_effect_streams[slot]);
    MilesStreamHandle stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->cancelled = true;
        state->active.store(false);
        stream = state->stream;
        state->stream = nullptr;
    }
    if (stream != nullptr) {
        if (!nonblocking_backend ||
            !close_mci_effect_stream_deferred(stream)) {
            PauseMilesStream(stream);
            CloseMilesStream(stream);
        }
    }
    return true;
}

int get_async_effect_slot_status(u32 slot) {
    if (slot >= kMilesEffectStreamSlots) {
        return 0;
    }
    const std::shared_ptr<AsyncMilesEffectStream>& state =
        g_async_effect_streams[slot];
    if (state == nullptr || !state->active.load()) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->stream == nullptr || state->paused) {
        return state->cancelled ? 0 : 1;
    }
    const int status = GetMilesStreamStatus(state->stream);
    if (status == 0) {
        state->active.store(false);
    }
    return status;
}

void pause_async_effect_slot(u32 slot) {
    if (slot >= kMilesEffectStreamSlots ||
        g_async_effect_streams[slot] == nullptr) {
        return;
    }
    const std::shared_ptr<AsyncMilesEffectStream>& state =
        g_async_effect_streams[slot];
    std::lock_guard<std::mutex> lock(state->mutex);
    state->paused = true;
    if (state->stream != nullptr && state->started) {
        PauseMilesStream(state->stream);
    }
}

void resume_async_effect_slot(u32 slot) {
    if (slot >= kMilesEffectStreamSlots ||
        g_async_effect_streams[slot] == nullptr) {
        return;
    }
    const std::shared_ptr<AsyncMilesEffectStream>& state =
        g_async_effect_streams[slot];
    std::lock_guard<std::mutex> lock(state->mutex);
    state->paused = false;
    if (state->stream == nullptr) {
        return;
    }
    if (state->started) {
        ResumeMilesStream(state->stream);
    }
    else {
        StartMilesStreamWithLoopCount(state->stream, 1);
        state->started = true;
    }
}

} // namespace

void AddDirectMilesEffectPath(const char* path) {
    add_effect_playlist_entry(MilesEffectEntryKind::DirectFile, path, 0);
}

bool AddMilesEffectArchiveRecord(const char* archive_name, u32 record_index) {
    return add_effect_playlist_entry(MilesEffectEntryKind::ArchiveRecord, archive_name,
        record_index);
}

void AddJw204MilesEffectRecord(u32 record_index, const char* label_path) {
    add_effect_playlist_entry(MilesEffectEntryKind::Jw204Record,
        label_path, record_index);
}

bool RemoveMilesEffectPlaylistEntry(i32 entry_index) {
    if (static_cast<i32>(g_effect_playlist_state.entries.size()) <= entry_index) {
        return false;
    }

    g_effect_playlist_state.entries.erase(
        g_effect_playlist_state.entries.begin() + entry_index);
    return true;
}

bool LoadMilesEffectPlaylistInfoFromTrc(const char* archive_name, u32 info_record_index) {
    ResetMilesEffectPlaylist();

    std::vector<u8> payload;
    if (!LoadTrcRecordAlloc(archive_name, info_record_index, payload) ||
        payload.size() < sizeof(u32)) {
        return true;
    }

    const u32 entry_count = read_le_u32(payload.data());
    if (entry_count == 0) {
        return true;
    }

    const std::size_t expected_size = sizeof(u32) +
        static_cast<std::size_t>(entry_count) * kMilesEffectPlaylistRecordBytes;
    if (payload.size() != expected_size) {
        return true;
    }

    try {
        g_effect_playlist_state.entries.reserve(entry_count);

        const u8* cursor = payload.data() + sizeof(u32);
        for (u32 i = 0; i < entry_count; ++i) {
            MilesEffectPlaylistEntry entry;
            entry.kind = static_cast<MilesEffectEntryKind>(read_le_u32(cursor));
            entry.path = fixed_playlist_path(cursor + sizeof(u32));
            entry.record_index =
                read_le_u32(cursor + sizeof(u32) + kMilesEffectPlaylistPathBytes);
            g_effect_playlist_state.entries.push_back(std::move(entry));
            cursor += kMilesEffectPlaylistRecordBytes;
        }
    } catch (const std::bad_alloc&) {
        ResetMilesEffectPlaylist();
        return false;
    }
    return true;
}

bool SaveMilesEffectPlaylistToTrc(const char* archive_name) {
    if (g_effect_playlist_state.entries.size() >
            static_cast<std::size_t>(0xffffffffu / kMilesEffectPlaylistRecordBytes)) {
        return false;
    }

    u32 existing_record_count = 0;
    QueryTrcArchiveRecordCount(archive_name, &existing_record_count, nullptr);
    u32 next_payload_record = existing_record_count + 1;

    try {
        std::vector<MilesEffectPlaylistEntry> serialized_entries =
            g_effect_playlist_state.entries;
        for (MilesEffectPlaylistEntry& entry : serialized_entries) {
            if (entry.kind == MilesEffectEntryKind::DirectFile) {
                entry.kind = MilesEffectEntryKind::ArchiveRecord;
                entry.path = archive_name;
                entry.record_index = next_payload_record;
                ++next_payload_record;
            } else if (entry.kind == MilesEffectEntryKind::ArchiveRecord) {
                ++next_payload_record;
            }
        }

        const u32 entry_count = static_cast<u32>(serialized_entries.size());
        const std::size_t payload_size = sizeof(u32) +
            static_cast<std::size_t>(entry_count) * kMilesEffectPlaylistRecordBytes;
        std::vector<u8> payload(payload_size, 0);
        write_le_u32(payload.data(), entry_count);

        u8* cursor = payload.data() + sizeof(u32);
        for (const MilesEffectPlaylistEntry& entry : serialized_entries) {
            write_effect_playlist_entry(cursor, entry);
            cursor += kMilesEffectPlaylistRecordBytes;
        }

        if (!HandleTrcMemoryRecordAppend(archive_name, "DLG_MP3_INFO", payload.data(),
                payload.size(), 0x32, 0)) {
            return false;
        }
    } catch (const std::bad_alloc&) {
        return false;
    }

    for (const MilesEffectPlaylistEntry& entry : g_effect_playlist_state.entries) {
        if (entry.kind == MilesEffectEntryKind::Empty) {
            return false;
        }
        if (entry.kind == MilesEffectEntryKind::DirectFile) {
            if (!AppendFilePayloadToTrcBuilder(archive_name, entry.path.c_str(), 0)) {
                return false;
            }
        } else if (entry.kind == MilesEffectEntryKind::ArchiveRecord) {
            if (!AppendArchiveRecordToTrcBuilder(archive_name, entry.path.c_str(),
                    entry.record_index, 0)) {
                return false;
            }
        }
    }
    return true;
}

void PlayMilesEffectPlaylistEntry(i32 entry_index) {
    if (static_cast<i32>(g_effect_playlist_state.entries.size()) <= entry_index) {
        return;
    }

    const MilesEffectPlaylistEntry& entry = g_effect_playlist_state.entries[entry_index];
    if (entry.kind == MilesEffectEntryKind::Empty) {
        return;
    }

    const u32 slot = acquire_effect_stream_slot();
    if (should_use_async_mci_effect_backend() &&
        start_async_mci_effect_stream(slot, entry_index, entry)) {
        return;
    }

    MilesStreamHandle& stream = g_effect_playlist_state.streams[slot];
    const bool opened = open_miles_effect_entry_stream(
        entry, g_effect_playlist_state.contexts[slot], &stream);

    if (!opened) {
        return;
    }

    StartMilesStreamWithLoopCount(stream, 1);
    g_effect_playlist_state.active_entry_indices[slot] = entry_index;
}

void CloseMilesEffectPlaylistEntry(i32 entry_index) {
    if (static_cast<i32>(g_effect_playlist_state.entries.size()) <= entry_index) {
        return;
    }

    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        if (g_effect_playlist_state.active_entry_indices[slot] == entry_index &&
            g_effect_playlist_state.streams[slot] != nullptr) {
            close_effect_slot(slot, true);
        }
    }
}

void StopMilesEffectPlaylistEntry(i32 entry_index) {
    if (static_cast<i32>(g_effect_playlist_state.entries.size()) <= entry_index) {
        return;
    }

    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        if (g_effect_playlist_state.active_entry_indices[slot] == entry_index &&
            g_effect_playlist_state.streams[slot] != nullptr) {
            if (g_async_effect_streams[slot] != nullptr) {
                pause_async_effect_slot(slot);
            }
            else {
                StopMilesStream(g_effect_playlist_state.streams[slot]);
            }
        }
    }
}

void ResumeMilesEffectPlaylistEntry(i32 entry_index) {
    if (static_cast<i32>(g_effect_playlist_state.entries.size()) <= entry_index) {
        return;
    }

    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        if (g_effect_playlist_state.active_entry_indices[slot] == entry_index &&
            g_effect_playlist_state.streams[slot] != nullptr) {
            if (g_async_effect_streams[slot] != nullptr) {
                resume_async_effect_slot(slot);
            }
            else {
                ResumeMilesStream(g_effect_playlist_state.streams[slot]);
            }
        }
    }
}

int GetMilesEffectPlaylistEntryStatus(i32 entry_index) {
    if (static_cast<i32>(g_effect_playlist_state.entries.size()) <= entry_index) {
        return 0;
    }

    int status = 0;
    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        if (g_effect_playlist_state.active_entry_indices[slot] == entry_index &&
            g_effect_playlist_state.streams[slot] != nullptr) {
            status |= (g_async_effect_streams[slot] != nullptr ?
                get_async_effect_slot_status(slot) :
                GetMilesStreamStatus(g_effect_playlist_state.streams[slot])) & 0xff;
        }
    }
    return status;
}

void CloseAllMilesEffectPlaylistStreams() {
    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        if (g_effect_playlist_state.active_entry_indices[slot] != kInvalidMilesEffectEntry &&
            g_effect_playlist_state.streams[slot] != nullptr) {
            // Session replacement is itself part of the scenario-loading
            // path.  Do not reintroduce the MCI_WAIT hitch there while
            // releasing the previous scenario's dialog streams.
            close_effect_slot(slot, true);
        }
    }
}

const MilesSoundState& miles_sound_state() {
    return g_miles_state;
}

MilesMusicRuntimeState& miles_music_state() {
    return g_music_state;
}

BriefingBinkMediaState& briefing_bink_media_state() {
    return g_briefing_bink_state;
}

MilesEffectPlaylistState& miles_effect_playlist_state() {
    return g_effect_playlist_state;
}

MilesComputedStreamContext& primary_miles_computed_stream_context() {
    return g_music_state.primary_context;
}

MilesComputedStreamContext& secondary_miles_computed_stream_context() {
    return g_music_state.secondary_context;
}

MilesComputedStreamContext& default_miles_computed_stream_context() {
    return g_default_computed_stream_context;
}

void InitializePrimaryMilesArchiveContext() {
    InitializeMilesTrcArchiveStreamContext(g_music_state.primary_context);
}

void ShutdownPrimaryMilesArchiveContext() {
    ReleaseMilesTrcArchiveStreamContext(g_music_state.primary_context);
}

void InitializeSecondaryMilesArchiveContext() {
    InitializeMilesTrcArchiveStreamContext(g_music_state.secondary_context);
}

void ShutdownSecondaryMilesArchiveContext() {
    ReleaseMilesTrcArchiveStreamContext(g_music_state.secondary_context);
}

void InitializeMilesEffectArchiveContexts() {
    for (MilesComputedStreamContext& context : g_effect_playlist_state.contexts) {
        InitializeMilesTrcArchiveStreamContext(context);
    }
}

void ShutdownMilesEffectArchiveContexts() {
    for (MilesComputedStreamContext& context : g_effect_playlist_state.contexts) {
        ReleaseMilesTrcArchiveStreamContext(context);
    }
}

void ConstructMilesEffectArchiveContextVector() {
    InitializeMilesEffectArchiveContexts();
}

void DestroyMilesEffectArchiveContextVector() {
    ShutdownMilesEffectArchiveContexts();
}

void RegisterMilesEffectArchiveContextsAtExit() {
    std::atexit(DestroyMilesEffectArchiveContextVector);
}

void InitializeAndRegisterMilesEffectArchiveContexts() {
    ConstructMilesEffectArchiveContextVector();
    RegisterMilesEffectArchiveContextsAtExit();
}

}
