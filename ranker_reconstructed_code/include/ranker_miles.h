#pragma once

#include "ranker_trc.h"
#include "ranker_types.h"

#include <array>
#include <string>
#include <vector>

namespace ranker {

using MilesStreamHandle = void*;

constexpr u32 kMilesEffectStreamSlots = 5;
constexpr u32 kMilesEffectPlaylistPathBytes = 0x104;
constexpr u32 kMilesEffectPlaylistRecordBytes = 0x10c;
constexpr u32 kInvalidMilesEffectEntry = 0xffffffffu;
constexpr u32 kPrimaryMilesMusicStoppedRecord = 0x13;
constexpr u32 kBriefingStartBinkRecord = 0x1b;
constexpr u32 kBriefingEndBinkRecord = 0x1c;
constexpr u32 kBriefingEffectPlaylistInfoRecord = 0x1d;

struct MilesComputedStreamContext {
    std::vector<u8> payload;
    std::vector<u8> compressed_payload;
    std::string archive_name;
    std::string computed_path;
    TrcDirectoryEntry directory_entry;
    u32 record_index = 0;
    u32 record_count = 0;
    u32 original_size = 0;
    u32 stored_size = 0;
    u32 read_offset = 0;
    u16 check_value = 0;
    u16 method = 0;
    bool open = false;
};

enum class MilesEffectEntryKind : u32 {
    Empty = 0,
    DirectFile = 1,
    ArchiveRecord = 2,
    Jw204Record = 3,
};

enum class BriefingBinkSourceKind : u32 {
    Empty = 0,
    DirectFile = 1,
    ArchiveRecord = 2,
    Jw208RecordPointer = 3,
};

struct BriefingBinkSource {
    BriefingBinkSourceKind kind = BriefingBinkSourceKind::Empty;
    std::string path;
    u32 record_index = 0;
};

struct BriefingBinkMediaState {
    std::string archive_name;
    BriefingBinkSource start;
    BriefingBinkSource end;
    u32 active_game_mode = 0;
};

struct MilesEffectPlaylistEntry {
    MilesEffectEntryKind kind = MilesEffectEntryKind::Empty;
    std::string path;
    u32 record_index = 0;
};

struct MilesEffectPlaylistState {
    std::vector<MilesEffectPlaylistEntry> entries;
    std::array<MilesStreamHandle, kMilesEffectStreamSlots> streams{};
    std::array<u32, kMilesEffectStreamSlots> active_entry_indices{
        kInvalidMilesEffectEntry, kInvalidMilesEffectEntry, kInvalidMilesEffectEntry,
        kInvalidMilesEffectEntry, kInvalidMilesEffectEntry};
    std::array<MilesComputedStreamContext, kMilesEffectStreamSlots> contexts;
};

struct MilesSoundState {
    void* module = nullptr;
    void* digital_driver = nullptr;
    u32 last_serve_time = 0;
    int last_status = 0;
    int last_volume_percent = 0;
    int last_current_ms = 0;
    int last_length_ms = 0;
    int last_playback_rate = 0;
    bool api_loaded = false;
};

struct MilesMusicRuntimeState {
    bool enabled = false;
    MilesStreamHandle primary_stream = nullptr;
    MilesStreamHandle secondary_stream = nullptr;
    MilesStreamHandle direct_stream = nullptr;
    MilesComputedStreamContext primary_context;
    MilesComputedStreamContext secondary_context;
    u32 primary_policy_mode = 0;
    u32 primary_policy_last_tick = 0;
    u32 primary_policy_record = kPrimaryMilesMusicStoppedRecord;
    u32 primary_policy_raw_volume = 0xffff;
    u32 primary_policy_faction_index = 0;
    std::array<u32, 8> primary_policy_faction_music_bases{};
};

bool InitMilesSoundSubsystem(const char* redist_directory, int sample_rate,
    int bits_per_sample, int channels);
void ShutdownMilesSoundSubsystem();

bool InitMilesMusicRuntime(const char* redist_directory = ".", int sample_rate = 0xac44,
    int bits_per_sample = 0x10, int channels = 2);
void ShutdownMilesMusicRuntime();
void InitializeAndRegisterPrimaryMilesArchiveContext();
void ConstructPrimaryMilesArchiveContext();
void RegisterPrimaryMilesArchiveContextAtExit();
void DestroyPrimaryMilesArchiveContext();
void InitializeAndRegisterSecondaryMilesArchiveContext();
void ConstructSecondaryMilesArchiveContext();
void RegisterSecondaryMilesArchiveContextAtExit();
void DestroySecondaryMilesArchiveContext();

bool OpenMilesStream(const char* path, MilesStreamHandle* stream_out);
void InitializeMilesTrcArchiveStreamContext(MilesComputedStreamContext& context);
void ReleaseMilesTrcArchiveStreamContext(MilesComputedStreamContext& context);
void DestroyMilesTrcArchiveStreamContext(MilesComputedStreamContext& context);
bool OpenMilesTrcRecordDirectoryEntry(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index);
bool LoadMilesTrcRecordIntoArchiveContext(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index);
bool ReadMilesTrcArchiveStreamBytes(MilesComputedStreamContext& context,
    void* out, u32 byte_count);
u32 QueryMilesTrcRecordOriginalSize(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index);
bool QueryMilesTrcRecordStoredAndOriginalSize(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, u32* stored_size, u32* original_size);
bool CopyMilesTrcRecordToBuffer(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, void* out);
bool LoadMilesTrcRecordAlloc(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, std::vector<u8>& out,
    std::size_t extra_bytes = 0);
bool ReadMilesTrcRecordWindowFromOpenArchive(MilesComputedStreamContext& context,
    u32 record_index, u32 unused, u32 byte_count, void* out);
bool OpenComputedMilesMp3Stream(MilesComputedStreamContext& context,
    const char* archive_name, u32 record_index, MilesStreamHandle* stream_out);
void CloseMilesStream(MilesStreamHandle stream);
void ResetMilesStreamArchiveState(MilesComputedStreamContext& context);
void ResetMilesStreamArchiveState();
void StartMilesStreamWithLoopCount(MilesStreamHandle stream, int loop_count);
void PauseMilesStream(MilesStreamHandle stream);
void StopMilesStream(MilesStreamHandle stream);
void ResumeMilesStream(MilesStreamHandle stream);
int GetMilesStreamStatus(MilesStreamHandle stream);
int GetMilesStreamVolume(MilesStreamHandle stream);
void SetMilesStreamVolume(MilesStreamHandle stream, int volume_percent);
void SetMilesSoundPreference(int value);
void ServeMilesSound();
int GetMilesStreamCurrentMs(MilesStreamHandle stream);
int GetMilesStreamLengthMs(MilesStreamHandle stream);
void SetMilesStreamMsPosition(MilesStreamHandle stream, int position_ms);
int GetMilesStreamPlaybackRate(MilesStreamHandle stream);
void SetMilesStreamPlaybackRate(MilesStreamHandle stream, int playback_rate);

void InitializePrimaryMilesMusicPolicy();
void ShutdownPrimaryMilesMusicPolicy();
void SetPrimaryMilesMusicPolicyMode(u32 mode);
void UpdatePrimaryMilesMusicPolicy();
void StopPrimaryMilesMusicPolicy();
void ResumePrimaryMilesMusicPolicy();
void ApplyPrimaryMilesMusicPolicyVolume();
void PausePrimaryMusicFromPolicy();
void HandlePrimaryMusicPolicyResume();
void HandlePrimaryMusicPolicyVolumeApply();
void SetPrimaryMilesMusicPolicyRawVolume(u32 raw_volume);
void SetPrimaryMilesMusicPolicyFactionIndex(u32 faction_index);
void SetPrimaryMilesMusicPolicyFactionBase(u32 faction_index, u32 record_base);

void PlayPrimaryMilesMusicRecord(u32 record_index, const char* archive_name = "JW2_15.TRC");
void ClosePrimaryMilesMusic();
void StopPrimaryMilesMusic();
void ResumePrimaryMilesMusic();
int GetPrimaryMilesMusicStatus();
void ApplyPrimaryMilesMusicVolume(u32 raw_volume);

void PlaySecondaryMilesMusicRecord(u32 record_index, const char* archive_name = "JW2_06.TRC");
void CloseSecondaryMilesMusic();

bool PlayDirectMilesMusic(const char* path);
void CloseDirectMilesMusic();
void StopDirectMilesMusic();
void ResumeDirectMilesMusic();
int GetDirectMilesMusicStatus();
int GetDirectMilesMusicVolume();
void SetDirectMilesMusicVolume(int volume_percent);
int GetDirectMilesMusicCurrentMs();
int GetDirectMilesMusicLengthMs();
void SetDirectMilesMusicMsPosition(int position_ms);
int GetDirectMilesMusicPlaybackRate();
void SetDirectMilesMusicPlaybackRate(int playback_rate);

void InitializeBriefingBinkMediaState();
void SetBriefingBinkArchiveName(const char* archive_name);
int CompareBriefingBinkArchiveName(const char* lhs, const char* rhs);
bool ExtractBriefingBinkRecordToTempFile(const char* archive_name, u32 record_index,
    std::string& temp_path);
bool MaterializeBriefingBinkSourcesForArchive(const char* archive_name);
void ResetBriefingStartBinkSource();
bool ResolveBriefingStartBinkSourceRecord(const char* archive_name);
bool LoadBriefingStartBinkSourceFromTrc(const char* archive_name);
bool SaveBriefingStartBinkSourceToTrc(const char* archive_name);
bool HandleBriefingStartVideoPlayback();
bool PlayBriefingStartBinkSource();
void ResetBriefingEndBinkSource();
bool ResolveBriefingEndBinkSourceRecord(const char* archive_name);
bool LoadBriefingEndBinkSourceFromTrc(const char* archive_name);
bool SaveBriefingEndBinkSourceToTrc(const char* archive_name);
bool HandleBriefingEndVideoPlayback();
bool PlayBriefingEndBinkSource();
void SetBriefingBinkActiveGameMode(u32 mode);
bool InitializeMilesEffectSoundSubsystem();
void HandleMilesEffectSoundShutdown();
void ShutdownMilesEffectSoundSubsystem();

void ResetMilesEffectPlaylist();
void AddDirectMilesEffectPath(const char* path);
bool AddMilesEffectArchiveRecord(const char* archive_name, u32 record_index);
void AddJw204MilesEffectRecord(u32 record_index, const char* label_path = "");
bool RemoveMilesEffectPlaylistEntry(i32 entry_index);
bool LoadMilesEffectPlaylistInfoFromTrc(const char* archive_name,
    u32 info_record_index = 0x1d);
bool SaveMilesEffectPlaylistToTrc(const char* archive_name);
void PlayMilesEffectPlaylistEntry(i32 entry_index);
void CloseMilesEffectPlaylistEntry(i32 entry_index);
void CloseMilesEffectPlaylistEntryDeferred(i32 entry_index);
void StopMilesEffectPlaylistEntry(i32 entry_index);
void ResumeMilesEffectPlaylistEntry(i32 entry_index);
int GetMilesEffectPlaylistEntryStatus(i32 entry_index);
void CloseAllMilesEffectPlaylistStreams();

const MilesSoundState& miles_sound_state();
MilesMusicRuntimeState& miles_music_state();
BriefingBinkMediaState& briefing_bink_media_state();
MilesEffectPlaylistState& miles_effect_playlist_state();
MilesComputedStreamContext& primary_miles_computed_stream_context();
MilesComputedStreamContext& secondary_miles_computed_stream_context();
MilesComputedStreamContext& default_miles_computed_stream_context();
void InitializePrimaryMilesArchiveContext();
void ShutdownPrimaryMilesArchiveContext();
void InitializeSecondaryMilesArchiveContext();
void ShutdownSecondaryMilesArchiveContext();
void InitializeMilesEffectArchiveContexts();
void ShutdownMilesEffectArchiveContexts();
void InitializeAndRegisterMilesEffectArchiveContexts();
void ConstructMilesEffectArchiveContextVector();
void RegisterMilesEffectArchiveContextsAtExit();
void DestroyMilesEffectArchiveContextVector();

}
