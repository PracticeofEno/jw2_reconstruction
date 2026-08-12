#include "ranker_visual_animation_archive.h"

#include "zlib.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ranker {
namespace {

constexpr std::array<u8, 8> kArchiveMagic{
    'R', '1', '4', '4', 'R', 'F', 'A', 0};
constexpr u32 kArchiveVersion = 2;
constexpr u32 kArchiveHeaderBytes = 104;
constexpr u32 kArchiveDirectoryEntryBytes = 40;
constexpr std::size_t kCacheEntryLimit = 512;
constexpr u64 kCacheByteLimit = 64ull * 1024ull * 1024ull;

struct TransitionKey {
    u16 unit_type = 0;
    u8 image_group = 0;
    u8 flipped = 0;
    u16 source_frame = 0;
    u16 target_frame = 0;

    bool operator==(const TransitionKey& other) const {
        return unit_type == other.unit_type &&
            image_group == other.image_group && flipped == other.flipped &&
            source_frame == other.source_frame &&
            target_frame == other.target_frame;
    }
};

bool key_less(const TransitionKey& left, const TransitionKey& right) {
    // The offline generator sorts (type, group, source, target, flipped).
    return std::tie(left.unit_type, left.image_group, left.source_frame,
               left.target_frame, left.flipped) <
        std::tie(right.unit_type, right.image_group, right.source_frame,
               right.target_frame, right.flipped);
}

struct TransitionKeyHash {
    std::size_t operator()(const TransitionKey& key) const {
        std::size_t value = key.unit_type;
        value = value * 131u + key.image_group;
        value = value * 131u + key.source_frame;
        value = value * 131u + key.target_frame;
        return value * 3u + key.flipped;
    }
};

struct DirectoryEntry {
    TransitionKey key{};
    i16 left = 0;
    i16 top = 0;
    u16 width = 0;
    u16 height = 0;
    u64 data_offset = 0;
    u32 stored_size = 0;
    u32 uncompressed_size = 0;
    u32 raw_crc32 = 0;
    i16 flip_origin_x = 0;
    i16 flip_delta_y = 0;
};

struct CachedTransition {
    std::vector<u8> bytes;
    std::array<std::size_t,
        kVisualAnimationArchiveIntermediateFrameCount> frame_offsets{};
    std::array<std::size_t,
        kVisualAnimationArchiveIntermediateFrameCount> frame_sizes{};
    u64 last_used = 0;
};

VisualAnimationArchiveState g_state;
std::ifstream g_archive_stream;
std::vector<DirectoryEntry> g_directory;
std::unordered_map<TransitionKey, CachedTransition, TransitionKeyHash> g_cache;
u64 g_cache_clock = 0;

u16 read_u16(const u8* data) {
    return static_cast<u16>(data[0]) |
        static_cast<u16>(static_cast<u16>(data[1]) << 8u);
}

i16 read_i16(const u8* data) {
    return static_cast<i16>(read_u16(data));
}

u32 read_u32(const u8* data) {
    return static_cast<u32>(data[0]) |
        (static_cast<u32>(data[1]) << 8u) |
        (static_cast<u32>(data[2]) << 16u) |
        (static_cast<u32>(data[3]) << 24u);
}

u64 read_u64(const u8* data) {
    return static_cast<u64>(read_u32(data)) |
        (static_cast<u64>(read_u32(data + 4)) << 32u);
}

u32 bytes_crc32(const u8* data, std::size_t size) {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (size != 0) {
        const uInt chunk = static_cast<uInt>(std::min<std::size_t>(
            size, std::numeric_limits<uInt>::max()));
        crc = crc32(crc, data, chunk);
        data += chunk;
        size -= chunk;
    }
    return static_cast<u32>(crc);
}

bool read_source_identity(
    const std::string& path, u64& size, u32& source_crc32) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::array<u8, 1024 * 1024> buffer{};
    uLong crc = crc32(0L, Z_NULL, 0);
    size = 0;
    while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize read = stream.gcount();
        if (read <= 0) {
            break;
        }
        crc = crc32(crc, buffer.data(), static_cast<uInt>(read));
        size += static_cast<u64>(read);
    }
    if (!stream.eof()) {
        return false;
    }
    source_crc32 = static_cast<u32>(crc);
    return true;
}

void set_failure(VisualAnimationArchiveStatus status, const char* detail) {
    g_state.status = status;
    g_state.detail = detail;
    g_archive_stream.close();
    g_directory.clear();
    ResetVisualAnimationArchiveCache();
}

bool validate_rle_frame(const u8* payload, std::size_t payload_size,
    u32 width, u32 height) {
    std::size_t cursor = 0;
    for (u32 row = 0; row < height; ++row) {
        if (cursor + 2 > payload_size) {
            return false;
        }
        const std::size_t encoded_size = read_u16(payload + cursor);
        cursor += 2;
        if (encoded_size > payload_size - cursor) {
            return false;
        }
        const std::size_t end = cursor + encoded_size;
        u32 remaining = width;
        while (cursor < end && remaining != 0) {
            const u8 token = payload[cursor++];
            if (token != 0) {
                --remaining;
                continue;
            }
            if (cursor >= end) {
                return false;
            }
            const u8 skip = payload[cursor++];
            if (skip == 0 || skip > remaining) {
                return false;
            }
            remaining -= skip;
        }
        if (cursor != end) {
            return false;
        }
    }
    return cursor == payload_size;
}

bool parse_cached_transition(const DirectoryEntry& entry,
    CachedTransition& transition) {
    std::size_t cursor = 0;
    for (u32 frame = 0;
         frame < kVisualAnimationArchiveIntermediateFrameCount; ++frame) {
        if (cursor + 4 > transition.bytes.size()) {
            return false;
        }
        const std::size_t frame_size = read_u32(transition.bytes.data() + cursor);
        cursor += 4;
        if (frame_size > transition.bytes.size() - cursor ||
            !validate_rle_frame(transition.bytes.data() + cursor, frame_size,
                entry.width, entry.height)) {
            return false;
        }
        transition.frame_offsets[frame] = cursor;
        transition.frame_sizes[frame] = frame_size;
        cursor += frame_size;
    }
    return cursor == transition.bytes.size();
}

void evict_cache(u64 incoming_bytes) {
    while (!g_cache.empty() &&
        (g_cache.size() >= kCacheEntryLimit ||
            g_state.cache_bytes + incoming_bytes > kCacheByteLimit)) {
        auto oldest = g_cache.begin();
        for (auto it = std::next(g_cache.begin()); it != g_cache.end(); ++it) {
            if (it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        g_state.cache_bytes -= oldest->second.bytes.size();
        g_cache.erase(oldest);
    }
    g_state.cache_entries = static_cast<u32>(g_cache.size());
}

const DirectoryEntry* find_directory_entry(const TransitionKey& key) {
    const auto found = std::lower_bound(g_directory.begin(), g_directory.end(), key,
        [](const DirectoryEntry& entry, const TransitionKey& requested) {
            return key_less(entry.key, requested);
        });
    return found != g_directory.end() && found->key == key ? &*found : nullptr;
}

CachedTransition* load_cached_transition(const DirectoryEntry& entry) {
    auto cached = g_cache.find(entry.key);
    if (cached != g_cache.end()) {
        cached->second.last_used = ++g_cache_clock;
        return &cached->second;
    }

    std::vector<u8> compressed(entry.stored_size);
    g_archive_stream.clear();
    g_archive_stream.seekg(static_cast<std::streamoff>(entry.data_offset));
    g_archive_stream.read(reinterpret_cast<char*>(compressed.data()),
        static_cast<std::streamsize>(compressed.size()));
    if (!g_archive_stream ||
        static_cast<std::size_t>(g_archive_stream.gcount()) != compressed.size()) {
        ++g_state.decompression_failures;
        return nullptr;
    }

    CachedTransition transition{};
    transition.bytes.resize(entry.uncompressed_size);
    uLongf destination_size = static_cast<uLongf>(transition.bytes.size());
    const int decompressed = uncompress(transition.bytes.data(), &destination_size,
        compressed.data(), static_cast<uLong>(compressed.size()));
    if (decompressed != Z_OK || destination_size != entry.uncompressed_size ||
        bytes_crc32(transition.bytes.data(), transition.bytes.size()) !=
            entry.raw_crc32 ||
        !parse_cached_transition(entry, transition)) {
        ++g_state.decompression_failures;
        return nullptr;
    }

    evict_cache(transition.bytes.size());
    transition.last_used = ++g_cache_clock;
    g_state.cache_bytes += transition.bytes.size();
    cached = g_cache.emplace(entry.key, std::move(transition)).first;
    g_state.cache_entries = static_cast<u32>(g_cache.size());
    return &cached->second;
}

} // namespace

bool LoadVisualAnimationArchive(
    const std::string& archive_path, const std::string& source_path) {
    UnloadVisualAnimationArchive();
    g_state.archive_path = archive_path;
    g_state.source_path = source_path;

    g_archive_stream.open(archive_path, std::ios::binary);
    if (!g_archive_stream) {
        set_failure(VisualAnimationArchiveStatus::file_unavailable,
            "animation archive is unavailable");
        return false;
    }
    g_archive_stream.seekg(0, std::ios::end);
    const std::streamoff file_end = g_archive_stream.tellg();
    if (file_end < static_cast<std::streamoff>(kArchiveHeaderBytes)) {
        set_failure(VisualAnimationArchiveStatus::invalid_archive,
            "animation archive header is truncated");
        return false;
    }
    const u64 file_size = static_cast<u64>(file_end);
    g_archive_stream.seekg(0);
    std::array<u8, kArchiveHeaderBytes> header{};
    g_archive_stream.read(reinterpret_cast<char*>(header.data()), header.size());
    if (!g_archive_stream ||
        !std::equal(kArchiveMagic.begin(), kArchiveMagic.end(), header.begin()) ||
        read_u32(header.data() + 8) != kArchiveVersion ||
        read_u32(header.data() + 12) != kArchiveHeaderBytes ||
        read_u32(header.data() + 20) != kArchiveDirectoryEntryBytes ||
        read_u32(header.data() + 96) != kVisualAnimationArchiveIntervalCount ||
        read_u32(header.data() + 100) !=
            kVisualAnimationArchiveIntermediateFrameCount) {
        set_failure(VisualAnimationArchiveStatus::invalid_archive,
            "animation archive header is incompatible");
        return false;
    }

    const u32 transition_count = read_u32(header.data() + 16);
    const u64 directory_offset = read_u64(header.data() + 24);
    const u64 payload_offset = read_u64(header.data() + 32);
    const u64 payload_size = read_u64(header.data() + 40);
    const u64 source_size = read_u64(header.data() + 48);
    const u32 source_crc32 = read_u32(header.data() + 88);
    const u32 directory_crc32 = read_u32(header.data() + 92);
    const u64 directory_size =
        static_cast<u64>(transition_count) * kArchiveDirectoryEntryBytes;
    if (transition_count == 0 || directory_offset != kArchiveHeaderBytes ||
        directory_size > file_size - directory_offset ||
        payload_offset != directory_offset + directory_size ||
        payload_offset > file_size || payload_size != file_size - payload_offset) {
        set_failure(VisualAnimationArchiveStatus::invalid_archive,
            "animation archive bounds are invalid");
        return false;
    }

    std::vector<u8> directory_bytes(static_cast<std::size_t>(directory_size));
    g_archive_stream.seekg(static_cast<std::streamoff>(directory_offset));
    g_archive_stream.read(reinterpret_cast<char*>(directory_bytes.data()),
        static_cast<std::streamsize>(directory_bytes.size()));
    if (!g_archive_stream ||
        bytes_crc32(directory_bytes.data(), directory_bytes.size()) !=
            directory_crc32) {
        set_failure(VisualAnimationArchiveStatus::invalid_archive,
            "animation archive directory checksum failed");
        return false;
    }

    g_directory.reserve(transition_count);
    for (u32 index = 0; index < transition_count; ++index) {
        const u8* raw = directory_bytes.data() +
            static_cast<std::size_t>(index) * kArchiveDirectoryEntryBytes;
        DirectoryEntry entry{};
        entry.key.unit_type = read_u16(raw);
        entry.key.image_group = raw[2];
        entry.key.flipped = raw[3];
        entry.key.source_frame = read_u16(raw + 4);
        entry.key.target_frame = read_u16(raw + 6);
        entry.left = read_i16(raw + 8);
        entry.top = read_i16(raw + 10);
        entry.width = read_u16(raw + 12);
        entry.height = read_u16(raw + 14);
        entry.data_offset = read_u64(raw + 16);
        entry.stored_size = read_u32(raw + 24);
        entry.uncompressed_size = read_u32(raw + 28);
        entry.raw_crc32 = read_u32(raw + 32);
        entry.flip_origin_x = read_i16(raw + 36);
        entry.flip_delta_y = read_i16(raw + 38);
        const u64 stored_end = entry.data_offset + entry.stored_size;
        if (entry.key.unit_type >= 170 || entry.key.image_group >= 14 ||
            entry.key.flipped > 1 || entry.width == 0 || entry.height == 0 ||
            entry.stored_size == 0 || entry.uncompressed_size == 0 ||
            entry.data_offset < payload_offset || stored_end < entry.data_offset ||
            stored_end > file_size ||
            (!g_directory.empty() &&
                !key_less(g_directory.back().key, entry.key))) {
            set_failure(VisualAnimationArchiveStatus::invalid_archive,
                "animation archive directory entry is invalid");
            return false;
        }
        g_directory.push_back(entry);
    }

    if (!source_path.empty()) {
        u64 actual_size = 0;
        u32 actual_crc32 = 0;
        if (!read_source_identity(source_path, actual_size, actual_crc32)) {
            set_failure(VisualAnimationArchiveStatus::file_unavailable,
                "authoritative Jw2_09.trc is unavailable");
            return false;
        }
        if (actual_size != source_size || actual_crc32 != source_crc32) {
            set_failure(VisualAnimationArchiveStatus::source_mismatch,
                "animation archive does not match Jw2_09.trc");
            return false;
        }
    }

    g_state.status = VisualAnimationArchiveStatus::loaded;
    g_state.detail = "pre-generated animation archive loaded";
    g_state.transition_count = transition_count;
    g_state.payload_bytes = payload_size;
    return true;
}

void UnloadVisualAnimationArchive() {
    g_archive_stream.close();
    g_directory.clear();
    g_cache.clear();
    g_cache_clock = 0;
    g_state = VisualAnimationArchiveState{};
}

void ResetVisualAnimationArchiveCache() {
    g_cache.clear();
    g_cache_clock = 0;
    g_state.cache_bytes = 0;
    g_state.cache_entries = 0;
}

bool FindVisualAnimationArchiveFrame(u32 unit_type, u32 image_group,
    u32 source_frame, u32 target_frame, u32 subframe_index, bool flipped,
    VisualAnimationArchiveFrameView& result) {
    result = {};
    if (g_state.status != VisualAnimationArchiveStatus::loaded ||
        unit_type > std::numeric_limits<u16>::max() || image_group > 0xffu ||
        source_frame > std::numeric_limits<u16>::max() ||
        target_frame > std::numeric_limits<u16>::max() ||
        subframe_index == 0 ||
        subframe_index > kVisualAnimationArchiveIntermediateFrameCount) {
        ++g_state.lookup_misses;
        return false;
    }
    const TransitionKey key{static_cast<u16>(unit_type),
        static_cast<u8>(image_group), 0,
        static_cast<u16>(source_frame), static_cast<u16>(target_frame)};
    const DirectoryEntry* entry = find_directory_entry(key);
    if (entry == nullptr) {
        ++g_state.lookup_misses;
        return false;
    }
    CachedTransition* transition = load_cached_transition(*entry);
    if (transition == nullptr) {
        ++g_state.lookup_misses;
        return false;
    }
    const std::size_t frame = subframe_index - 1;
    result.left = entry->left;
    result.top = entry->top;
    result.width = entry->width;
    result.height = entry->height;
    result.payload = transition->bytes.data() + transition->frame_offsets[frame];
    result.payload_size = transition->frame_sizes[frame];
    result.flip_origin_x = entry->flip_origin_x;
    result.flip_delta_y = entry->flip_delta_y;
    result.flipped = flipped;
    ++g_state.lookup_hits;
    return true;
}

const VisualAnimationArchiveState& visual_animation_archive_state() {
    return g_state;
}

} // namespace ranker
