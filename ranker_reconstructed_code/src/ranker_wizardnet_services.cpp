#include "ranker_wizardnet_services.h"

#ifdef _WIN32

#include "ranker_replay.h"
#include "ranker_wizardnet_relay.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstring>
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
    if (path == nullptr || *path == '\0') {
        return "Replay.ply";
    }

    // Replay paths come from the original ANSI/CP_ACP game UI.  Constructing
    // a std::filesystem::path here makes MinGW try to transcode those bytes
    // through the C locale, which throws for Korean map names during the
    // post-game upload and prevents the client from returning to WizardNet.
    // The relay protocol also expects the original legacy bytes, so extract
    // the leaf name without changing its encoding.
    std::string result(path);
    const std::size_t separator = result.find_last_of("\\/");
    if (separator != std::string::npos) {
        result.erase(0, separator + 1);
    }
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
    std::vector<u8> packet = build_packet(kWizardNetReplayUploadBeginOpcode, 0xb5);
    write_u32(packet, 0x0d, state.upload_id);
    write_u32(packet, 0x11, static_cast<u32>(state.replay_bytes.size()));
    write_u32(packet, 0x15, state.game_type);
    write_u32(packet, 0x19, static_cast<u32>(state.outcome));
    write_u32(packet, 0x1d, state.game_id);
    std::memcpy(packet.data() + 0x21, state.match_token.data(),
        state.match_token.size());
    copy_fixed(packet, 0x31, 0x80, state.display_name.c_str());
    write_u32(packet, 0xb1, state.duration_seconds);
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

std::string ansi_path_parent(std::string path) {
    while (path.size() > 3 &&
        (path.back() == '\\' || path.back() == '/')) {
        path.pop_back();
    }
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return {};
    }
    if (separator == 2 && path.size() >= 2 && path[1] == ':') {
        return path.substr(0, 3);
    }
    return path.substr(0, separator);
}

std::string append_ansi_path(const std::string& directory,
    const std::string& leaf) {
    if (directory.empty()) {
        return leaf;
    }
    if (directory.back() == '\\' || directory.back() == '/') {
        return directory + leaf;
    }
    return directory + "\\" + leaf;
}

bool ansi_directory_exists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string current_directory_ansi() {
    const DWORD required = GetCurrentDirectoryA(0, nullptr);
    if (required == 0) {
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(required) + 1, 0);
    const DWORD written = GetCurrentDirectoryA(
        static_cast<DWORD>(buffer.size()), buffer.data());
    return written != 0 && written < buffer.size() ?
        std::string(buffer.data(), written) : std::string{};
}

std::string resolve_replays_directory() {
    std::vector<std::string> roots;
    std::array<char, MAX_PATH> module_path{};
    const DWORD module_length = GetModuleFileNameA(nullptr, module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (module_length != 0 && module_length < module_path.size()) {
        roots.push_back(ansi_path_parent(
            std::string(module_path.data(), module_length)));
    }
    const std::string initial_current = current_directory_ansi();
    std::string current = initial_current;
    for (int index = 0; index < 7 && !current.empty(); ++index) {
        const auto duplicate = std::find_if(roots.begin(), roots.end(),
            [&](const std::string& root) {
                return _stricmp(root.c_str(), current.c_str()) == 0;
            });
        if (duplicate == roots.end()) {
            roots.push_back(current);
        }
        const std::string parent = ansi_path_parent(current);
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }
    for (const std::string& root : roots) {
        for (const std::string& candidate : {
                 append_ansi_path(root, "Replays"),
                 append_ansi_path(append_ansi_path(root, "ranker"),
                     "Replays")}) {
            if (ansi_directory_exists(candidate)) {
                return candidate;
            }
        }
    }
    return append_ansi_path(initial_current.empty() ? "." : initial_current,
        "Replays");
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
    std::vector<u8> packet = build_packet(kWizardNetReplayListRequestOpcode, 0x15);
    write_u32(packet, 0x0d, offset);
    write_u32(packet, 0x11, kWizardNetReplayListProtocolVersion);
    return packet;
}

std::vector<u8> BuildWizardNetReplayDownloadRequestPacket(u32 replay_id) {
    std::vector<u8> packet = build_packet(kWizardNetReplayDownloadRequestOpcode, 0x11);
    write_u32(packet, 0x0d, replay_id);
    return packet;
}

std::vector<u8> BuildWizardNetReplayPresenceRequestPacket(bool active) {
    std::vector<u8> packet = build_packet(
        kWizardNetReplayPresenceRequestOpcode, 0x11);
    write_u32(packet, 0x0d, active ? 1u : 0u);
    return packet;
}

bool BeginWizardNetPostGameSubmission(LegacyAsyncTcpSocket& socket,
    u32 game_type, u32 gameplay_result, u32 game_id,
    const std::array<u8, kWizardNetMatchTokenBytes>& match_token,
    const char* replay_path, u32 duration_seconds) {
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

    if (!ShouldAutoUploadWizardNetReplay(game_type)) {
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
    pending.duration_seconds = duration_seconds;
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
    try {
        // WizardNet names are legacy ANSI/CP949 bytes.  MinGW's
        // std::filesystem path conversion uses the C++ locale and throws for
        // those names.  The original game stays on Win32 ANSI file APIs, so
        // preserve the wire bytes unchanged at this boundary as well.
        const std::string directory = append_ansi_path(
            resolve_replays_directory(), "download");
        if (!CreateDirectoryA(directory.c_str(), nullptr)) {
            const DWORD create_error = GetLastError();
            if (create_error != ERROR_ALREADY_EXISTS ||
                !ansi_directory_exists(directory)) {
                return false;
            }
        }

        std::string leaf = SanitizeReplayFilenameComponent(filename);
        const bool ply_extension = leaf.size() >= 4 &&
            _stricmp(leaf.c_str() + leaf.size() - 4, ".ply") == 0;
        if (!ply_extension) {
            leaf += ".ply";
        }
        const std::size_t extension_offset = leaf.size() - 4;
        const std::string stem = leaf.substr(0, extension_offset);
        const std::string extension = leaf.substr(extension_offset);
        std::string candidate = append_ansi_path(directory, leaf);
        for (u32 suffix = 2;; ++suffix) {
            const DWORD attributes = GetFileAttributesA(candidate.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                const DWORD query_error = GetLastError();
                if (query_error == ERROR_FILE_NOT_FOUND ||
                    query_error == ERROR_PATH_NOT_FOUND) {
                    break;
                }
                return false;
            }
            candidate = append_ansi_path(directory,
                stem + "_" + std::to_string(suffix) + extension);
        }

        const std::string temporary = candidate + ".part";
        HANDLE output = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) {
            return false;
        }
        bool write_ok = true;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(output, bytes.data() + offset, requested,
                    &written, nullptr) || written == 0) {
                write_ok = false;
                break;
            }
            offset += written;
        }
        if (!CloseHandle(output)) {
            write_ok = false;
        }
        if (!write_ok) {
            DeleteFileA(temporary.c_str());
            return false;
        }
        if (!MoveFileExA(temporary.c_str(), candidate.c_str(),
                MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temporary.c_str());
            return false;
        }

        const DWORD absolute_bytes = GetFullPathNameA(
            candidate.c_str(), 0, nullptr, nullptr);
        if (absolute_bytes != 0) {
            std::vector<char> absolute(
                static_cast<std::size_t>(absolute_bytes) + 1, 0);
            const DWORD written = GetFullPathNameA(candidate.c_str(),
                static_cast<DWORD>(absolute.size()), absolute.data(), nullptr);
            if (written != 0 && written < absolute.size()) {
                output_path.assign(absolute.data(), written);
            }
        }
        if (output_path.empty()) {
            output_path = candidate;
        }
        return true;
    }
    catch (...) {
        output_path.clear();
        return false;
    }
}

} // namespace ranker

#endif
