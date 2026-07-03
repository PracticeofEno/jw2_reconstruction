#include "ranker_miles.h"

#include "ranker_trc.h"
#include "ranker_win32_compat.h"

#ifdef _WIN32
#include "ranker_cursor.h"
#include "ranker_directx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace ranker {
namespace {

MilesSoundState g_miles_state;
MilesMusicRuntimeState g_music_state;
BriefingBinkMediaState g_briefing_bink_state;
MilesEffectPlaylistState g_effect_playlist_state;
MilesComputedStreamContext g_default_computed_stream_context;

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

void close_effect_slot(u32 slot) {
    if (slot >= kMilesEffectStreamSlots) {
        return;
    }

    MilesStreamHandle& stream = g_effect_playlist_state.streams[slot];
    if (stream != nullptr) {
        PauseMilesStream(stream);
        CloseMilesStream(stream);
        stream = nullptr;
    }
    g_effect_playlist_state.active_entry_indices[slot] = kInvalidMilesEffectEntry;
}

u32 acquire_effect_stream_slot() {
    for (u32 i = 0; i < kMilesEffectStreamSlots; ++i) {
        if (g_effect_playlist_state.streams[i] == nullptr) {
            return i;
        }
    }

    const u32 slot = static_cast<u32>(std::rand()) % kMilesEffectStreamSlots;
    close_effect_slot(slot);
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
#endif

} // namespace

bool InitMilesSoundSubsystem(const char* redist_directory, int sample_rate,
    int bits_per_sample, int channels) {
#ifdef _WIN32
    ShutdownMilesSoundSubsystem();
    if (!load_miles_api()) {
        unload_miles_module();
        return false;
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
    if (!load_miles_api()) {
        return false;
    }

    void* stream = g_miles_api.open_stream(g_miles_state.digital_driver, path, 0);
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
    if (load_miles_api()) {
        g_miles_api.pause_stream(stream, 0);
    }
#else
    (void)stream;
#endif
}

int GetMilesStreamStatus(MilesStreamHandle stream) {
#ifdef _WIN32
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
    if (g_miles_state.digital_driver == nullptr || !load_miles_api()) {
        return;
    }

    const u32 now = timeGetTime();
    if (now - g_miles_state.last_serve_time > 99) {
        g_miles_api.serve();
        g_miles_state.last_serve_time = now;
    }
#endif
}

int GetMilesStreamCurrentMs(MilesStreamHandle stream) {
#ifdef _WIN32
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
        if (primary_is_playing()) {
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
    MilesStreamHandle& stream = g_effect_playlist_state.streams[slot];
    bool opened = false;
    switch (entry.kind) {
    case MilesEffectEntryKind::DirectFile:
        opened = OpenMilesStream(entry.path.c_str(), &stream);
        break;
    case MilesEffectEntryKind::ArchiveRecord:
        opened = OpenComputedMilesMp3Stream(g_effect_playlist_state.contexts[slot],
            entry.path.c_str(), entry.record_index, &stream);
        break;
    case MilesEffectEntryKind::Jw204Record:
        opened = OpenComputedMilesMp3Stream(g_effect_playlist_state.contexts[slot],
            "JW2_04.TRC", entry.record_index, &stream);
        break;
    case MilesEffectEntryKind::Empty:
    default:
        opened = false;
        break;
    }

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
            close_effect_slot(slot);
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
            StopMilesStream(g_effect_playlist_state.streams[slot]);
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
            ResumeMilesStream(g_effect_playlist_state.streams[slot]);
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
            status |= GetMilesStreamStatus(g_effect_playlist_state.streams[slot]) & 0xff;
        }
    }
    return status;
}

void CloseAllMilesEffectPlaylistStreams() {
    for (u32 slot = 0; slot < kMilesEffectStreamSlots; ++slot) {
        if (g_effect_playlist_state.active_entry_indices[slot] != kInvalidMilesEffectEntry &&
            g_effect_playlist_state.streams[slot] != nullptr) {
            close_effect_slot(slot);
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
