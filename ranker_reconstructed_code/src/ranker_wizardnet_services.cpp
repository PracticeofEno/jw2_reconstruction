#include "ranker_wizardnet_services.h"

#ifdef _WIN32

#include "ranker_replay.h"
#include "ranker_wizardnet_relay.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace ranker {
namespace {

constexpr std::size_t kLegacyHeaderBytes = 0x0d;
constexpr i32 kUploadPumpPendingByteLimit = 256 * 1024;

WizardNetReplayUploadState g_upload_state;

void write_u32(std::vector<u8>& packet, std::size_t offset, u32 value) {
    if (offset + sizeof(value) <= packet.size()) {
        std::memcpy(packet.data() + offset, &value, sizeof(value));
    }
}

void write_u64(std::vector<u8>& packet, std::size_t offset, u64 value) {
    if (offset + sizeof(value) <= packet.size()) {
        std::memcpy(packet.data() + offset, &value, sizeof(value));
    }
}

std::vector<u8> build_packet(u32 opcode, std::size_t byte_count) {
    std::vector<u8> packet(std::max(byte_count, kLegacyHeaderBytes), 0);
    write_u32(packet, 0, 3);
    write_u32(packet, 4, opcode);
    write_u32(packet, 8, static_cast<u32>(packet.size()));
    return packet;
}

void copy_fixed(std::vector<u8>& packet, std::size_t offset,
    std::size_t byte_count, const char* text) {
    if (offset >= packet.size() || byte_count == 0) {
        return;
    }
    const std::size_t available = std::min(byte_count, packet.size() - offset);
    std::memset(packet.data() + offset, 0, available);
    if (text != nullptr && available > 1) {
        std::strncpy(reinterpret_cast<char*>(packet.data() + offset), text,
            available - 1);
    }
}

bool queue_packet(LegacyAsyncTcpSocket& socket, std::vector<u8>& packet) {
    return QueueWizardNetAsyncTcpPacket(socket, packet.data(),
        static_cast<u32>(packet.size()));
}

std::string replay_leaf_name(const char* path) {
    namespace fs = std::filesystem;
    if (path == nullptr || *path == '\0') {
        return "Replay.ply";
    }
    std::string result = fs::path(path).filename().string();
    return result.empty() ? "Replay.ply" : result;
}

bool load_replay_file(const char* path, std::vector<u8>& bytes) {
    bytes.clear();
    if (path == nullptr || *path == '\0') {
        return false;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) >
            kWizardNetMaximumReplayBytes) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input) {
        bytes.clear();
        return false;
    }
    return true;
}

std::vector<u8> build_upload_begin_packet(
    const WizardNetReplayUploadState& state) {
    std::vector<u8> packet = build_packet(kWizardNetReplayUploadBeginOpcode, 0xb1);
    write_u32(packet, 0x0d, state.upload_id);
    write_u32(packet, 0x11, static_cast<u32>(state.replay_bytes.size()));
    write_u32(packet, 0x15, state.game_type);
    write_u32(packet, 0x19, static_cast<u32>(state.outcome));
    write_u32(packet, 0x1d, state.game_id);
    std::memcpy(packet.data() + 0x21, state.match_token.data(),
        state.match_token.size());
    copy_fixed(packet, 0x31, 0x80, state.display_name.c_str());
    return packet;
}

std::vector<u8> build_upload_chunk_packet(
    const WizardNetReplayUploadState& state, std::size_t byte_count) {
    std::vector<u8> packet = build_packet(kWizardNetReplayUploadChunkOpcode,
        0x15 + byte_count);
    write_u32(packet, 0x0d, state.upload_id);
    write_u32(packet, 0x11, static_cast<u32>(state.next_offset));
    std::memcpy(packet.data() + 0x15,
        state.replay_bytes.data() + state.next_offset, byte_count);
    return packet;
}

std::vector<u8> build_upload_end_packet(
    const WizardNetReplayUploadState& state) {
    std::vector<u8> packet = build_packet(kWizardNetReplayUploadEndOpcode, 0x1d);
    write_u32(packet, 0x0d, state.upload_id);
    write_u32(packet, 0x11, static_cast<u32>(state.replay_bytes.size()));
    write_u64(packet, 0x15, state.hash_value);
    return packet;
}

std::filesystem::path resolve_replays_directory() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    std::array<char, MAX_PATH> module_path{};
    const DWORD module_length = GetModuleFileNameA(nullptr, module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (module_length != 0 && module_length < module_path.size()) {
        roots.push_back(fs::path(module_path.data()).parent_path());
    }
    std::error_code error;
    fs::path current = fs::current_path(error);
    for (int index = 0; !error && index < 7 && !current.empty(); ++index) {
        if (std::find(roots.begin(), roots.end(), current) == roots.end()) {
            roots.push_back(current);
        }
        current = current.parent_path();
    }
    for (const fs::path& root : roots) {
        for (const fs::path& candidate : {
                 root / "Replays", root / "ranker" / "Replays"}) {
            if (fs::is_directory(candidate, error) && !error) {
                return candidate;
            }
            error.clear();
        }
    }
    return fs::current_path() / "Replays";
}

} // namespace

WizardNetReplayUploadState& wizardnet_replay_upload_state() {
    return g_upload_state;
}

u64 WizardNetReplayFnv1a64(const void* data, std::size_t byte_count, u64 value) {
    if (data == nullptr) {
        return value;
    }
    const auto* bytes = static_cast<const u8*>(data);
    for (std::size_t index = 0; index < byte_count; ++index) {
        value ^= bytes[index];
        value *= 0x100000001b3ull;
    }
    return value;
}

std::vector<u8> BuildWizardNetMatchResultPacket(u32 game_type,
    WizardNetMatchOutcome outcome, u32 game_id,
    const std::array<u8, kWizardNetMatchTokenBytes>& match_token) {
    std::vector<u8> packet = build_packet(kWizardNetMatchResultRequestOpcode, 0x29);
    write_u32(packet, 0x0d, game_type);
    write_u32(packet, 0x11, static_cast<u32>(outcome));
    write_u32(packet, 0x15, game_id);
    std::memcpy(packet.data() + 0x19, match_token.data(), match_token.size());
    return packet;
}

std::vector<u8> BuildWizardNetReplayListRequestPacket(u32 offset) {
    std::vector<u8> packet = build_packet(kWizardNetReplayListRequestOpcode, 0x11);
    write_u32(packet, 0x0d, offset);
    return packet;
}

std::vector<u8> BuildWizardNetReplayDownloadRequestPacket(u32 replay_id) {
    std::vector<u8> packet = build_packet(kWizardNetReplayDownloadRequestOpcode, 0x11);
    write_u32(packet, 0x0d, replay_id);
    return packet;
}

bool BeginWizardNetPostGameSubmission(LegacyAsyncTcpSocket& socket,
    u32 game_type, u32 gameplay_result, u32 game_id,
    const std::array<u8, kWizardNetMatchTokenBytes>& match_token,
    bool room_host, const char* replay_path) {
    if ((!UsesWizardNetNormalGameStatistics(game_type) &&
            !UsesWizardNetRankingStatistics(game_type)) || game_id == 0) {
        return false;
    }

    const WizardNetMatchOutcome outcome =
        WizardNetOutcomeFromGameplayResult(gameplay_result);
    std::vector<u8> result_packet = BuildWizardNetMatchResultPacket(
        game_type, outcome, game_id, match_token);
    if (!queue_packet(socket, result_packet)) {
        return false;
    }

    if (!room_host || !ShouldAutoUploadWizardNetReplay(game_type)) {
        return true;
    }

    WizardNetReplayUploadState pending{};
    if (!load_replay_file(replay_path, pending.replay_bytes)) {
        return true;
    }
    pending.active = true;
    pending.upload_id = GetTickCount() ^ GetCurrentProcessId() ^ game_id;
    if (pending.upload_id == 0) {
        pending.upload_id = 1;
    }
    pending.game_type = game_type;
    pending.game_id = game_id;
    pending.outcome = outcome;
    pending.match_token = match_token;
    pending.display_name = replay_leaf_name(replay_path);
    pending.hash_value = WizardNetReplayFnv1a64(
        pending.replay_bytes.data(), pending.replay_bytes.size());
    g_upload_state = std::move(pending);
    PumpWizardNetReplayUpload(socket);
    return true;
}

bool PumpWizardNetReplayUpload(LegacyAsyncTcpSocket& socket) {
    WizardNetReplayUploadState& state = g_upload_state;
    if (!state.active) {
        return false;
    }
    if (!state.begin_queued) {
        std::vector<u8> begin = build_upload_begin_packet(state);
        if (!queue_packet(socket, begin)) {
            return true;
        }
        state.begin_queued = true;
    }

    while (state.next_offset < state.replay_bytes.size()) {
        if (socket.send_length >= kUploadPumpPendingByteLimit) {
            return true;
        }
        const std::size_t byte_count = std::min(
            kWizardNetReplayTransferChunkBytes,
            state.replay_bytes.size() - state.next_offset);
        std::vector<u8> chunk = build_upload_chunk_packet(state, byte_count);
        if (!queue_packet(socket, chunk)) {
            return true;
        }
        state.next_offset += byte_count;
    }
    if (state.next_offset < state.replay_bytes.size()) {
        return true;
    }

    std::vector<u8> end = build_upload_end_packet(state);
    if (!queue_packet(socket, end)) {
        return true;
    }
    state = WizardNetReplayUploadState{};
    return false;
}

void HandleWizardNetReplayUploadStatus(u32 status, u32 upload_id) {
    if (status != 0 && g_upload_state.active &&
        (upload_id == 0 || upload_id == g_upload_state.upload_id)) {
        ResetWizardNetReplayUploadState();
    }
}

void ResetWizardNetReplayUploadState() {
    g_upload_state = WizardNetReplayUploadState{};
}

bool SaveWizardNetDownloadedReplay(const std::string& filename,
    const std::vector<u8>& bytes, std::string& output_path) {
    output_path.clear();
    if (bytes.empty() || bytes.size() > kWizardNetMaximumReplayBytes) {
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code error;
    fs::path directory = resolve_replays_directory() / "download";
    fs::create_directories(directory, error);
    if (error) {
        return false;
    }

    const std::string safe_name = SanitizeReplayFilenameComponent(filename);
    fs::path candidate = directory / safe_name;
    if (candidate.extension().string() != ".ply" &&
        candidate.extension().string() != ".PLY") {
        candidate += ".ply";
    }
    const fs::path requested = candidate;
    for (u32 suffix = 2; fs::exists(candidate, error) && !error; ++suffix) {
        candidate = directory /
            (requested.stem().string() + "_" + std::to_string(suffix) +
                requested.extension().string());
    }
    if (error) {
        return false;
    }

    fs::path temporary = candidate;
    temporary += ".part";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        fs::remove(temporary, error);
        return false;
    }
    fs::rename(temporary, candidate, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    output_path = fs::absolute(candidate, error).string();
    return !error;
}

} // namespace ranker

#endif
