#pragma once

#include "ranker_network.h"
#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

constexpr u32 kWizardNetGameTypeTopVsBottom = 0;
constexpr u32 kWizardNetGameTypeMelee = 1;
constexpr u32 kWizardNetGameTypeRank = 2;

constexpr u32 kWizardNetMatchResultRequestOpcode = 0x98;
constexpr u32 kWizardNetMatchResultResponseOpcode = 0x99;
constexpr u32 kWizardNetReplayUploadBeginOpcode = 0x9a;
constexpr u32 kWizardNetReplayUploadChunkOpcode = 0x9b;
constexpr u32 kWizardNetReplayUploadEndOpcode = 0x9c;
constexpr u32 kWizardNetReplayUploadStatusOpcode = 0x9d;
constexpr u32 kWizardNetReplayListRequestOpcode = 0x9e;
constexpr u32 kWizardNetReplayListResponseOpcode = 0x9f;
constexpr u32 kWizardNetReplayDownloadRequestOpcode = 0xa0;
constexpr u32 kWizardNetReplayDownloadChunkOpcode = 0xa1;
constexpr u32 kWizardNetReplayDownloadFinishOpcode = 0xa2;
constexpr std::size_t kWizardNetMatchTokenBytes = 16;
constexpr std::size_t kWizardNetReplayTransferChunkBytes = 32 * 1024;
constexpr std::size_t kWizardNetMaximumReplayBytes = 64 * 1024 * 1024;

enum class WizardNetMatchOutcome : u32 {
    Win = 0,
    Loss = 1,
    Draw = 2,
};

constexpr bool IsWizardNetRankGameType(u32 game_type) {
    return game_type == kWizardNetGameTypeRank;
}

constexpr bool UsesWizardNetNormalGameStatistics(u32 game_type) {
    return game_type == kWizardNetGameTypeTopVsBottom ||
        game_type == kWizardNetGameTypeMelee;
}

constexpr bool UsesWizardNetRankingStatistics(u32 game_type) {
    return IsWizardNetRankGameType(game_type);
}

constexpr bool ShouldAutoUploadWizardNetReplay(u32 game_type) {
    return game_type == kWizardNetGameTypeMelee ||
        game_type == kWizardNetGameTypeRank;
}

constexpr WizardNetMatchOutcome WizardNetOutcomeFromGameplayResult(
    u32 result_code) {
    return result_code == 0 ? WizardNetMatchOutcome::Win :
        result_code == 2 ? WizardNetMatchOutcome::Draw :
        WizardNetMatchOutcome::Loss;
}

struct WizardNetReplayUploadState {
    bool active = false;
    bool begin_queued = false;
    u32 upload_id = 0;
    u32 game_type = 0;
    u32 game_id = 0;
    WizardNetMatchOutcome outcome = WizardNetMatchOutcome::Draw;
    std::array<u8, kWizardNetMatchTokenBytes> match_token{};
    std::string display_name;
    std::vector<u8> replay_bytes;
    std::size_t next_offset = 0;
    u64 hash_value = 0;
};

WizardNetReplayUploadState& wizardnet_replay_upload_state();
u64 WizardNetReplayFnv1a64(const void* data, std::size_t byte_count,
    u64 value = 0xcbf29ce484222325ull);
std::vector<u8> BuildWizardNetMatchResultPacket(u32 game_type,
    WizardNetMatchOutcome outcome, u32 game_id,
    const std::array<u8, kWizardNetMatchTokenBytes>& match_token);
std::vector<u8> BuildWizardNetReplayListRequestPacket(u32 offset);
std::vector<u8> BuildWizardNetReplayDownloadRequestPacket(u32 replay_id);
bool BeginWizardNetPostGameSubmission(LegacyAsyncTcpSocket& socket,
    u32 game_type, u32 gameplay_result, u32 game_id,
    const std::array<u8, kWizardNetMatchTokenBytes>& match_token,
    const char* replay_path);
bool PumpWizardNetReplayUpload(LegacyAsyncTcpSocket& socket);
void HandleWizardNetReplayUploadStatus(u32 status, u32 upload_id);
void ResetWizardNetReplayUploadState();
bool SaveWizardNetDownloadedReplay(const std::string& filename,
    const std::vector<u8>& bytes, std::string& output_path);

} // namespace ranker
