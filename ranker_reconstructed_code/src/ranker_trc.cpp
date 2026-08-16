#include "ranker_trc.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <zlib.h>

namespace ranker {
namespace {

constexpr std::array<u8, 4> kTrcMagic{'T', 'R', 'C', 0x1a};
constexpr std::size_t kTrcHeaderSize = 0x20;
constexpr std::size_t kTrcEntrySize = 0x20;
constexpr int kZlibOk = 0;
constexpr int kZlibBufError = -5;
constexpr int kZlibVersionError = -6;
constexpr int kZlibDefaultCompression = -1;
constexpr u32 kTrcBuilderDirectoryGrowth = 0x14;
constexpr std::array<u8, 5> kLegacyTrcNullFallback{'N', 'U', 'L', 'L', 0};

using ZlibBufferLength = uLongf;

#ifdef _WIN32
using ZlibUncompressProc = int(__cdecl*)(u8*, ZlibBufferLength*, const u8*, ZlibBufferLength);
using ZlibCompress2Proc = int(__cdecl*)(u8*, ZlibBufferLength*, const u8*, ZlibBufferLength, int);

struct ZlibDynamicApi {
    HMODULE module = nullptr;
    ZlibUncompressProc uncompress = nullptr;
    ZlibCompress2Proc compress2 = nullptr;
    bool attempted = false;
};
#endif

struct PreparedTrcWriteRecord {
    std::string name;
    std::vector<u8> stored_payload;
    u32 original_size = 0;
    u16 check_value = 0;
    u16 method = 0;
    u32 reserved = 0;
};

struct CachedArchiveBytes {
    std::filesystem::file_time_type last_write_time{};
    std::uintmax_t file_size = 0;
    std::shared_ptr<const std::vector<u8>> data;
};

using ArchiveCacheKey = std::filesystem::path::string_type;

std::mutex g_archive_cache_mutex;
std::unordered_map<ArchiveCacheKey, CachedArchiveBytes> g_archive_cache;

u16 read_le_u16(const u8* p) {
    return static_cast<u16>(p[0]) | static_cast<u16>(p[1] << 8);
}

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

void write_le_u16(u8* p, u16 value) {
    p[0] = static_cast<u8>(value & 0xff);
    p[1] = static_cast<u8>((value >> 8) & 0xff);
}

void write_le_u32(u8* p, u32 value) {
    p[0] = static_cast<u8>(value & 0xff);
    p[1] = static_cast<u8>((value >> 8) & 0xff);
    p[2] = static_cast<u8>((value >> 16) & 0xff);
    p[3] = static_cast<u8>((value >> 24) & 0xff);
}

u16 byte_sum_checksum(const std::vector<u8>& payload) {
    u16 checksum = 0;
    for (u8 byte : payload) {
        checksum = static_cast<u16>(checksum + byte);
    }
    return checksum;
}

u16 signed_byte_sum_checksum(const u8* payload, std::size_t payload_size) {
    u16 checksum = 0;
    for (std::size_t i = 0; i < payload_size; ++i) {
        checksum = static_cast<u16>(checksum + static_cast<i8>(payload[i]));
    }
    return checksum;
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        const char la = static_cast<char>((ca >= 'A' && ca <= 'Z') ? ca + 32 : ca);
        const char lb = static_cast<char>((cb >= 'A' && cb <= 'Z') ? cb + 32 : cb);
        if (la != lb) {
            return false;
        }
    }

    return true;
}

std::filesystem::path filesystem_path_from_legacy_bytes(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
#ifdef _WIN32
    // Original lobby/session paths are ANSI bytes (CP_ACP), not UTF-8.  The
    // narrow std::filesystem constructor uses the C++ locale under MinGW and
    // throws for names such as "Prison II" whose suffix is U+2161 in CP949.
    // Convert through Win32 explicitly so every legacy map filename reaches
    // the wide Windows filesystem API without a locale-dependent round trip.
    const int required = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, value, -1,
            wide.data(), required) <= 0) {
        return {};
    }
    wide.resize(static_cast<std::size_t>(required - 1));
    return std::filesystem::path(std::move(wide));
#else
    return std::filesystem::path(value);
#endif
}

bool path_filename_equals_ignore_case(const std::filesystem::path& left,
    const std::filesystem::path& right) {
#ifdef _WIN32
    const std::wstring left_name = left.filename().native();
    const std::wstring right_name = right.filename().native();
    if (left_name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        right_name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(left_name.data(), static_cast<int>(left_name.size()),
        right_name.data(), static_cast<int>(right_name.size()), TRUE) == CSTR_EQUAL;
#else
    return equals_ignore_case(left.filename().string(), right.filename().string());
#endif
}

std::optional<std::filesystem::path> find_archive_path(const char* archive_name) {
    namespace fs = std::filesystem;

    const fs::path direct = filesystem_path_from_legacy_bytes(archive_name);
    if (direct.empty()) {
        return std::nullopt;
    }

    // Native lobby/map dialogs are allowed to change the process current
    // directory.  A second P2P session can therefore begin from an unrelated
    // map folder even though the TRC archives still live beside the game
    // executable.  Keep that executable directory as the stable first lookup
    // root; current-directory candidates remain for development builds that
    // run from a separate build tree.
    std::vector<fs::path> roots;
#ifdef _WIN32
    std::array<wchar_t, 32768> module_path{};
    const DWORD module_length = GetModuleFileNameW(nullptr, module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (module_length != 0 && module_length < module_path.size()) {
        const fs::path module_directory =
            fs::path(std::wstring(module_path.data(), module_length)).parent_path();
        roots.push_back(module_directory);
        roots.push_back(module_directory / "RankerOCPV_Win");
        roots.push_back(module_directory.parent_path() / "RankerOCPV_Win");
    }
#endif

    std::error_code current_ec;
    const fs::path current = fs::current_path(current_ec);
    if (!current_ec) {
        roots.push_back(current);
        roots.push_back(current.parent_path());
        roots.push_back(current.parent_path().parent_path());
        roots.push_back(current.parent_path().parent_path().parent_path());
    }
    const fs::path source_root =
        fs::path{__FILE__}.parent_path().parent_path().parent_path();
    roots.push_back(source_root);
    roots.push_back(source_root / "RankerOCPV_Win");

    for (const auto& root : roots) {
        if (root.empty()) {
            continue;
        }

        const auto candidate = root / direct;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            return candidate;
        }

        ec.clear();
        for (const auto& item : fs::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (item.is_regular_file() &&
                path_filename_equals_ignore_case(item.path(), direct)) {
                return item.path();
            }
        }
    }

    return std::nullopt;
}

std::string trc_name_from_slot(const u8* raw) {
    const char* begin = reinterpret_cast<const char*>(raw);
    const char* end = begin + 12;
    const char* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
}

bool parse_directory_entry(const std::vector<u8>& data, std::size_t slot,
    TrcDirectoryEntry& entry) {
    const std::size_t offset = kTrcHeaderSize + slot * kTrcEntrySize;
    if (offset + kTrcEntrySize > data.size()) {
        return false;
    }

    const u8* raw = data.data() + offset;
    entry.name = trc_name_from_slot(raw);
    entry.relative_offset = read_le_u32(raw + 0x0c);
    entry.original_size = read_le_u32(raw + 0x10);
    entry.stored_size = read_le_u32(raw + 0x14);
    entry.check_value = read_le_u16(raw + 0x18);
    entry.method = read_le_u16(raw + 0x1a);
    entry.reserved = read_le_u32(raw + 0x1c);
    return true;
}

bool read_all_bytes(const std::filesystem::path& path, std::vector<u8>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

ArchiveCacheKey archive_cache_key(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return absolute.native();
    }
    const auto fallback = std::filesystem::absolute(path, ec);
    return ec ? path.native() : fallback.native();
}

void invalidate_archive_read_cache_after_write() {
    // The original opens every save archive afresh.  The reconstruction keeps
    // immutable archive bytes for the render/import hot paths, but a save can
    // replace a file with the same length inside the filesystem timestamp's
    // observable granularity.  Size + mtime alone can then make a later Load
    // reuse the previous game's bytes.  Writes are rare, so clearing the small
    // process cache here preserves original save/load semantics without
    // penalizing ordinary record reads.
    std::lock_guard<std::mutex> lock(g_archive_cache_mutex);
    g_archive_cache.clear();
}

bool read_all_bytes_cached(const std::filesystem::path& path,
    std::shared_ptr<const std::vector<u8>>& data) {
    data.reset();

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    const auto last_write_time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    const ArchiveCacheKey key = archive_cache_key(path);

    {
        std::lock_guard<std::mutex> lock(g_archive_cache_mutex);
        const auto it = g_archive_cache.find(key);
        if (it != g_archive_cache.end() &&
            it->second.file_size == file_size &&
            it->second.last_write_time == last_write_time &&
            it->second.data != nullptr) {
            data = it->second.data;
            return true;
        }
    }

    std::vector<u8> loaded;
    if (!read_all_bytes(path, loaded)) {
        return false;
    }

    auto shared = std::make_shared<std::vector<u8>>(std::move(loaded));
    {
        std::lock_guard<std::mutex> lock(g_archive_cache_mutex);
        g_archive_cache[key] = CachedArchiveBytes{last_write_time, file_size, shared};
    }
    data = std::move(shared);
    return true;
}

bool read_archive_and_directory_entry(const char* archive_name, u32 record_index,
    std::vector<u8>& data, TrcDirectoryEntry& entry, u32& data_offset,
    std::string* resolved_archive_path = nullptr) {
    const auto archive_path = find_archive_path(archive_name);
    if (!archive_path) {
        return false;
    }
    if (resolved_archive_path != nullptr) {
        *resolved_archive_path = archive_path->string();
    }

    if (!read_all_bytes(*archive_path, data) || data.size() < kTrcHeaderSize) {
        return false;
    }

    const u32 directory_slots = read_le_u32(data.data() + 0x04);
    const u32 active_entries = read_le_u32(data.data() + 0x08);
    data_offset = read_le_u32(data.data() + 0x0c);
    if (record_index >= active_entries) {
        return false;
    }

    (void)directory_slots;
    const std::size_t directory_entry_end = kTrcHeaderSize +
        (static_cast<std::size_t>(record_index) + 1u) * kTrcEntrySize;
    if (directory_entry_end > data.size()) {
        return false;
    }

    return parse_directory_entry(data, record_index, entry);
}

bool read_cached_archive_and_directory_entry(const char* archive_name, u32 record_index,
    std::shared_ptr<const std::vector<u8>>& data, TrcDirectoryEntry& entry,
    u32& data_offset, std::string* resolved_archive_path = nullptr) {
    const auto archive_path = find_archive_path(archive_name);
    if (!archive_path) {
        return false;
    }
    if (resolved_archive_path != nullptr) {
        *resolved_archive_path = archive_path->string();
    }

    if (!read_all_bytes_cached(*archive_path, data) || data == nullptr ||
        data->size() < kTrcHeaderSize) {
        return false;
    }

    const u32 directory_slots = read_le_u32(data->data() + 0x04);
    const u32 active_entries = read_le_u32(data->data() + 0x08);
    data_offset = read_le_u32(data->data() + 0x0c);
    if (record_index >= active_entries) {
        return false;
    }

    (void)directory_slots;
    const std::size_t directory_entry_end = kTrcHeaderSize +
        (static_cast<std::size_t>(record_index) + 1u) * kTrcEntrySize;
    if (directory_entry_end > data->size()) {
        return false;
    }

    return parse_directory_entry(*data, record_index, entry);
}

const std::vector<u8>& reader_archive_bytes(const TrcRecordReader& reader) {
    if (reader.archive_data_ref != nullptr) {
        return *reader.archive_data_ref;
    }
    return reader.archive_data;
}

bool read_trc_record_stored_bytes(const char* archive_name, u32 record_index,
    std::vector<u8>& out, TrcDirectoryEntry* entry_out = nullptr) {
    out.clear();

    std::vector<u8> data;
    TrcDirectoryEntry entry;
    u32 data_offset = 0;
    if (!read_archive_and_directory_entry(archive_name, record_index, data, entry,
            data_offset)) {
        return false;
    }

    const std::size_t payload_offset =
        static_cast<std::size_t>(data_offset) + entry.relative_offset;
    if (payload_offset > data.size() ||
        entry.stored_size > data.size() - payload_offset) {
        return false;
    }

    out.assign(data.begin() + payload_offset,
        data.begin() + payload_offset + entry.stored_size);
    if (entry_out != nullptr) {
        *entry_out = entry;
    }
    return true;
}

bool read_archive_record_manifest(const char* archive_name, std::vector<TrcDirectoryEntry>& entries,
    u32& directory_slots) {
    entries.clear();
    directory_slots = 0;

    const auto archive_path = find_archive_path(archive_name);
    if (!archive_path) {
        return false;
    }

    std::vector<u8> data;
    if (!read_all_bytes(*archive_path, data) || data.size() < kTrcHeaderSize) {
        return false;
    }
    if (!std::equal(kTrcMagic.begin(), kTrcMagic.end(), data.begin())) {
        return false;
    }

    directory_slots = read_le_u32(data.data() + 0x04);
    const u32 active_entries = read_le_u32(data.data() + 0x08);
    const u32 data_offset = read_le_u32(data.data() + 0x0c);
    if (active_entries > directory_slots) {
        return false;
    }

    const std::size_t directory_end = kTrcHeaderSize +
        static_cast<std::size_t>(directory_slots) * kTrcEntrySize;
    if (directory_end > data.size() || data_offset < directory_end || data_offset > data.size()) {
        return false;
    }

    entries.reserve(active_entries);
    for (u32 i = 0; i < active_entries; ++i) {
        TrcDirectoryEntry entry;
        if (!parse_directory_entry(data, i, entry)) {
            entries.clear();
            return false;
        }
        entries.push_back(std::move(entry));
    }
    return true;
}

bool read_trc_write_records(const char* archive_name, std::vector<TrcWriteRecord>& records,
    u32& directory_slots) {
    records.clear();

    std::vector<TrcDirectoryEntry> entries;
    if (!read_archive_record_manifest(archive_name, entries, directory_slots)) {
        return false;
    }

    records.reserve(entries.size());
    for (u32 i = 0; i < entries.size(); ++i) {
        std::vector<u8> payload;
        TrcDirectoryEntry loaded_entry;
        if (!read_trc_record(archive_name, i, payload, &loaded_entry)) {
            records.clear();
            return false;
        }

        TrcWriteRecord record;
        record.name = loaded_entry.name;
        record.payload = std::move(payload);
        record.method = loaded_entry.method;
        record.reserved = loaded_entry.reserved;
        record.original_size = loaded_entry.original_size;
        record.has_original_size = true;
        record.check_value = loaded_entry.check_value;
        record.has_check_value = true;
        if (loaded_entry.method == 2) {
            std::vector<u8> stored_payload;
            if (!read_trc_record_stored_bytes(archive_name, i, stored_payload, nullptr)) {
                records.clear();
                return false;
            }
            record.stored_payload = std::move(stored_payload);
            record.has_stored_payload = true;
        }
        records.push_back(std::move(record));
    }
    return true;
}

std::string make_trc_record_name(const char* name) {
    if (name == nullptr) {
        return {};
    }

    std::string result{name};
    if (result.size() > 12) {
        result.resize(12);
    }
    return result;
}

bool load_trc_records_for_append(const char* archive_name, std::vector<TrcWriteRecord>& records,
    u32& directory_slots) {
    records.clear();
    directory_slots = 0;

    const auto archive_path = find_archive_path(archive_name);
    if (!archive_path) {
        return true;
    }

    return read_trc_write_records(archive_name, records, directory_slots);
}

bool append_trc_record(const char* archive_name, TrcWriteRecord record, u32 directory_growth) {
    if (archive_name == nullptr || record.name.empty()) {
        return false;
    }

    std::vector<TrcWriteRecord> records;
    u32 directory_slots = 0;
    if (!load_trc_records_for_append(archive_name, records, directory_slots)) {
        return false;
    }

    records.push_back(std::move(record));
    if (directory_growth == 0) {
        directory_growth = kTrcBuilderDirectoryGrowth;
    }
    while (records.size() > directory_slots) {
        directory_slots += directory_growth;
    }
    return WriteTrcRecords(archive_name, records, directory_slots);
}

#ifdef _WIN32
ZlibDynamicApi& zlib_dynamic_api() {
    static ZlibDynamicApi api;
    if (api.attempted) {
        return api;
    }

    api.attempted = true;
    constexpr std::array<const char*, 3> dll_names{"zlib1.dll", "libz.dll", "zlib.dll"};
    for (const char* dll_name : dll_names) {
        api.module = LoadLibraryA(dll_name);
        if (api.module != nullptr) {
            break;
        }
    }
    if (api.module == nullptr) {
        return api;
    }

    api.uncompress = reinterpret_cast<ZlibUncompressProc>(
        GetProcAddress(api.module, "uncompress"));
    api.compress2 = reinterpret_cast<ZlibCompress2Proc>(
        GetProcAddress(api.module, "compress2"));
    return api;
}
#endif

bool compress_record_payload(const std::vector<u8>& source, std::vector<u8>& compressed) {
    const u32 source_size = static_cast<u32>(source.size());
    u32 compressed_size = ZlibCompressBound113(source_size);
    if (compressed_size == 0) {
        return false;
    }

    compressed.assign(compressed_size, 0);
    const int result = ZlibCompress113(compressed.data(), &compressed_size,
        source.data(), source_size);
    if (result != kZlibOk || compressed_size > compressed.size()) {
        compressed.clear();
        return false;
    }

    compressed.resize(compressed_size);
    return true;
}

bool prepare_trc_write_records(const std::vector<TrcWriteRecord>& records,
    std::vector<PreparedTrcWriteRecord>& prepared) {
    prepared.clear();
    prepared.reserve(records.size());

    for (const TrcWriteRecord& record : records) {
        if (record.name.empty() || record.name.size() > 12 ||
            record.payload.size() > static_cast<std::size_t>(0xffffffffu) ||
            record.stored_payload.size() > static_cast<std::size_t>(0xffffffffu)) {
            return false;
        }

        PreparedTrcWriteRecord out;
        out.name = record.name;
        out.original_size = record.has_original_size ? record.original_size :
            static_cast<u32>(record.payload.size());
        out.check_value = record.has_check_value ? record.check_value :
            byte_sum_checksum(record.payload);
        out.method = record.method;
        out.reserved = record.reserved;

        if (record.method == 0) {
            out.stored_payload = record.payload;
        }
        else if (record.method == 2) {
            if (record.has_stored_payload) {
                out.stored_payload = record.stored_payload;
            }
            else if (!compress_record_payload(record.payload, out.stored_payload)) {
                return false;
            }
        }
        else {
            return false;
        }

        prepared.push_back(std::move(out));
    }
    return true;
}

char ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

void ascii_upper_in_place(char* value) {
    for (; *value != '\0'; ++value) {
        *value = ascii_upper(*value);
    }
}

bool is_ascii_upper_alpha(char value) {
    return value >= 'A' && value <= 'Z';
}

int hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return 0;
}

std::array<std::array<char, 26>, 13> build_key_substitution_tables() {
    constexpr char kTableBytesHex[] =
        "2039383720203020202032202031202020332020343536202020000000000000"
        "2033202032203120203034352020202020382039202020373620000000000000"
        "3631203339202034203520202030203820202020202020323720000000000000"
        "2020332020342020203620203539202020382037202031322030000000000000"
        "3220203520202020202034333820202030203631203720202039000000000000"
        "3720203120362033202020342030202020202039202038203532000000000000"
        "3839202020203435202020203736203020202033202031202032000000000000"
        "2020383736203520203420203332202031302020202039202020000000000000"
        "2020203531203820202036302037203420202033203920203220000000000000"
        "2020373820203920202033342020203536202030202031322020000000000000"
        "2039352020202032203637203820203320203120202020343020000000000000"
        "3120203736202032202020302038203933202020342020203520000000000000"
        "2020302020312020202020202020202032373533393634202038000000000000";

    std::array<std::array<char, 26>, 13> tables{};
    for (std::size_t applied_index = 0; applied_index < tables.size(); ++applied_index) {
        const std::size_t memory_index = tables.size() - 1 - applied_index;
        for (std::size_t i = 0; i < tables[applied_index].size(); ++i) {
            const std::size_t byte_index = memory_index * 32 + i;
            const int high = hex_nibble(kTableBytesHex[byte_index * 2]);
            const int low = hex_nibble(kTableBytesHex[byte_index * 2 + 1]);
            tables[applied_index][i] = static_cast<char>((high << 4) | low);
        }
    }
    return tables;
}

const std::array<std::array<char, 26>, 13>& key_substitution_tables() {
    static const auto tables = build_key_substitution_tables();
    return tables;
}

bool substitute_key_char(char& value, std::size_t table_index) {
    value = ascii_upper(value);
    if (!is_ascii_upper_alpha(value) || table_index >= key_substitution_tables().size()) {
        return false;
    }

    value = key_substitution_tables()[table_index][static_cast<std::size_t>(value - 'A')];
    return true;
}

bool apply_key_substitution_tables(char (&group1)[5], char (&group2)[5], char (&group3)[6]) {
    std::size_t table_index = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!substitute_key_char(group1[i], table_index++)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (!substitute_key_char(group2[i], table_index++)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 5; ++i) {
        if (!substitute_key_char(group3[i], table_index++)) {
            return false;
        }
    }
    return true;
}

u32 pow10_u32(u32 exponent) {
    u32 result = 1;
    while (exponent-- != 0) {
        result *= 10;
    }
    return result;
}

u32 decimal_digit(u32 value, u32 position) {
    return (value / pow10_u32(position)) % 10;
}

void swap_decimal_digit(u32& lhs, u32 lhs_position, u32& rhs, u32 rhs_position) {
    const u32 lhs_factor = pow10_u32(lhs_position);
    const u32 rhs_factor = pow10_u32(rhs_position);
    const u32 lhs_digit = (lhs / lhs_factor) % 10;
    const u32 rhs_digit = (rhs / rhs_factor) % 10;

    lhs = lhs - lhs_digit * lhs_factor + rhs_digit * lhs_factor;
    rhs = rhs - rhs_digit * rhs_factor + lhs_digit * rhs_factor;
}

void swap_bit(u32& lhs, u8 lhs_bit, u32& rhs, u8 rhs_bit) {
    const u32 lhs_mask = 1u << (lhs_bit & 0x1f);
    const u32 rhs_mask = 1u << (rhs_bit & 0x1f);
    const bool lhs_set = (lhs & lhs_mask) != 0;
    const bool rhs_set = (rhs & rhs_mask) != 0;

    lhs &= ~lhs_mask;
    rhs &= ~rhs_mask;
    if (rhs_set) {
        lhs |= lhs_mask;
    }
    if (lhs_set) {
        rhs |= rhs_mask;
    }
}

bool validate_encoded_trc_key(char (&group1)[5], char (&group2)[5], char (&group3)[6]) {
    if (!apply_key_substitution_tables(group1, group2, group3)) {
        return false;
    }

    u32 first = static_cast<u32>(std::atoi(group1));
    u32 second = static_cast<u32>(std::atoi(group2));
    u32 third = static_cast<u32>(std::atoi(group3));

    SwapDecimalDigit(first, 4, third, 2);
    SwapDecimalDigit(first, 2, third, 5);
    SwapDecimalDigit(second, 3, third, 3);
    SwapDecimalDigit(second, 1, third, 4);
    SwapBitBetweenValues(first, 2, second, 0);
    SwapBitBetweenValues(second, 2, third, 3);
    SwapBitBetweenValues(first, 0, third, 0);

    const u32 first_thousands = decimal_digit(first, 3);
    const u32 first_hundreds = decimal_digit(first, 2);
    const u32 first_tens = decimal_digit(first, 1);
    const u32 first_ones = decimal_digit(first, 0);
    const u32 second_thousands = decimal_digit(second, 3);
    const u32 second_hundreds = decimal_digit(second, 2);
    const u32 second_tens = decimal_digit(second, 1);
    const u32 second_ones = decimal_digit(second, 0);
    const u32 third_ten_thousands = decimal_digit(third, 4);
    const u32 third_thousands = decimal_digit(third, 3);
    const u32 third_hundreds = decimal_digit(third, 2);
    const u32 third_tens = decimal_digit(third, 1);
    const u32 third_ones = decimal_digit(third, 0);

    const u32 check0 =
        (first_hundreds ^ second_hundreds ^ second_ones ^ first_thousands ^ 9u) % 10u;
    const u32 check1 =
        (first_tens ^ first_ones ^ second_thousands ^ second_tens ^ third_ten_thousands ^ 5u) %
        10u;
    const u32 check2 =
        (second_hundreds ^ second_tens ^ first_thousands ^ (third_ten_thousands + second_ones)) %
        10u;
    const u32 check3 =
        (second_hundreds ^ second_tens ^ first_thousands ^ (third_thousands + second_hundreds)) %
        10u;
    const u32 check4 = (third_ten_thousands + third_thousands + third_hundreds + third_tens) %
        10u;

    return check0 == third_ten_thousands &&
        check1 == third_thousands &&
        check2 == third_hundreds &&
        check3 == third_tens &&
        check4 == third_ones;
}

} // namespace

bool IsZlibRuntimeAvailable() {
#ifdef _WIN32
    const auto& api = zlib_dynamic_api();
    if (api.uncompress != nullptr && api.compress2 != nullptr) {
        return true;
    }
#endif
    return true;
}

u32 ZlibCompressBound113(u32 source_len) {
    constexpr u32 kSlack = 64;
    const u64 bound = static_cast<u64>(source_len) + source_len / 8u + kSlack;
    if (bound > std::numeric_limits<u32>::max()) {
        return 0;
    }
    return static_cast<u32>(bound);
}

int ZlibUncompress113(void* destination, u32* destination_len, const void* source,
    u32 source_len) {
    if (destination_len == nullptr || (destination == nullptr && *destination_len != 0) ||
        (source == nullptr && source_len != 0)) {
        return kZlibBufError;
    }

#ifdef _WIN32
    const auto& api = zlib_dynamic_api();
    if (api.uncompress != nullptr) {
        ZlibBufferLength out_len = static_cast<ZlibBufferLength>(*destination_len);
        const int result = api.uncompress(static_cast<u8*>(destination), &out_len,
            static_cast<const u8*>(source), static_cast<ZlibBufferLength>(source_len));
        *destination_len = static_cast<u32>(out_len);
        return result;
    }
#endif

    ZlibBufferLength out_len = static_cast<ZlibBufferLength>(*destination_len);
    const int result = ::uncompress(static_cast<Bytef*>(destination), &out_len,
        static_cast<const Bytef*>(source), static_cast<ZlibBufferLength>(source_len));
    *destination_len = static_cast<u32>(out_len);
    return result;
}

int ZlibCompress113WithLevel(void* destination, u32* destination_len, const void* source,
    u32 source_len, int compression_level) {
    if (destination_len == nullptr || (destination == nullptr && *destination_len != 0) ||
        (source == nullptr && source_len != 0)) {
        return kZlibBufError;
    }

#ifdef _WIN32
    const auto& api = zlib_dynamic_api();
    if (api.compress2 != nullptr) {
        ZlibBufferLength out_len = static_cast<ZlibBufferLength>(*destination_len);
        const int result = api.compress2(static_cast<u8*>(destination), &out_len,
            static_cast<const u8*>(source), static_cast<ZlibBufferLength>(source_len),
            compression_level);
        *destination_len = static_cast<u32>(out_len);
        return result;
    }
#endif

    ZlibBufferLength out_len = static_cast<ZlibBufferLength>(*destination_len);
    const int result = ::compress2(static_cast<Bytef*>(destination), &out_len,
        static_cast<const Bytef*>(source), static_cast<ZlibBufferLength>(source_len),
        compression_level);
    *destination_len = static_cast<u32>(out_len);
    return result;
}

int ZlibCompress113(void* destination, u32* destination_len, const void* source,
    u32 source_len) {
    return ZlibCompress113WithLevel(destination, destination_len, source, source_len,
        kZlibDefaultCompression);
}

bool OpenTrcRecordDirectoryEntry(TrcRecordReader& reader, const char* archive_name,
    u32 record_index) {
    CloseTrcRecordReader(reader);
    reader.archive_name = archive_name != nullptr ? archive_name : "";
    reader.record_index = record_index;
    if (archive_name == nullptr) {
        return false;
    }

    if (!read_cached_archive_and_directory_entry(archive_name, record_index,
            reader.archive_data_ref, reader.entry, reader.data_offset,
            &reader.archive_path)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    reader.directory_open = true;
    return true;
}

bool OpenTrcRecordPayload(TrcRecordReader& reader) {
    if (!reader.directory_open) {
        return false;
    }

    reader.payload_offset = reader.data_offset + reader.entry.relative_offset;
    reader.payload.clear();
    reader.cursor = 0;
    const auto& archive_data = reader_archive_bytes(reader);
    if (reader.entry.method == 2) {
        const std::size_t payload_offset = reader.payload_offset;
        const std::size_t payload_end = payload_offset + reader.entry.stored_size;
        if (payload_end < payload_offset || payload_end > archive_data.size()) {
            return false;
        }
        reader.payload.assign(reader.entry.original_size, 0);
        u32 output_len = reader.entry.original_size;
        const int result = ZlibUncompress113(reader.payload.data(), &output_len,
            archive_data.data() + payload_offset, reader.entry.stored_size);
        if (result != kZlibOk || output_len > reader.payload.size()) {
            reader.payload.clear();
            return false;
        }
        reader.payload.resize(output_len);
        reader.entry.original_size = output_len;
    }

    reader.payload_open = true;
    return true;
}

bool ReadOpenTrcRecordBytes(TrcRecordReader& reader, void* out, std::size_t byte_count) {
    if (!reader.payload_open || (out == nullptr && byte_count != 0)) {
        return false;
    }
    if (reader.entry.method == 1) {
        return false;
    }
    if (reader.entry.method != 0 && reader.entry.method != 2) {
        return true;
    }
    const auto& archive_data = reader_archive_bytes(reader);
    if (reader.entry.method == 0) {
        const std::size_t read_offset =
            static_cast<std::size_t>(reader.payload_offset) + reader.cursor;
        if (read_offset < reader.payload_offset || read_offset > archive_data.size() ||
            byte_count > archive_data.size() - read_offset) {
            CloseTrcRecordReader(reader);
            return false;
        }

        if (byte_count != 0) {
            std::memcpy(out, archive_data.data() + read_offset, byte_count);
        }
        reader.cursor += byte_count;
        return true;
    }
    if (reader.cursor > reader.payload.size() ||
        byte_count > reader.payload.size() - reader.cursor) {
        CloseTrcRecordReader(reader);
        return false;
    }

    if (byte_count != 0) {
        std::memcpy(out, reader.payload.data() + reader.cursor, byte_count);
    }
    reader.cursor += byte_count;
    return true;
}

void CloseTrcRecordReader(TrcRecordReader& reader) {
    reader.archive_path.clear();
    reader.archive_data_ref.reset();
    reader.archive_data.clear();
    reader.payload.clear();
    reader.directory_open = false;
    reader.payload_open = false;
}

bool LoadTrcRecordIntoBuffer(const char* archive_name, u32 record_index, void* out,
    std::size_t out_capacity, std::size_t* bytes_read, TrcDirectoryEntry* entry) {
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    if (out == nullptr && out_capacity != 0) {
        return false;
    }

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader) ||
        reader.entry.original_size > out_capacity) {
        CloseTrcRecordReader(reader);
        return false;
    }

    const std::size_t byte_count = reader.entry.original_size;
    if (!ReadOpenTrcRecordBytes(reader, out, byte_count)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    if (bytes_read != nullptr) {
        *bytes_read = byte_count;
    }
    if (entry != nullptr) {
        *entry = reader.entry;
    }
    CloseTrcRecordReader(reader);
    return true;
}

bool read_trc_record(const char* archive_name, u32 record_index, std::vector<u8>& out,
    TrcDirectoryEntry* entry_out) {
    out.clear();

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return false;
    }

    out.assign(reader.entry.original_size, 0);
    if (!ReadOpenTrcRecordBytes(reader, out.data(), out.size())) {
        CloseTrcRecordReader(reader);
        out.clear();
        return false;
    }
    if (entry_out != nullptr) {
        *entry_out = reader.entry;
    }
    CloseTrcRecordReader(reader);
    return true;
}

bool read_trc_record_bytes(const char* archive_name, u32 record_index, void* out,
    std::size_t out_capacity, std::size_t* bytes_read, TrcDirectoryEntry* entry) {
    return LoadTrcRecordIntoBuffer(archive_name, record_index, out, out_capacity, bytes_read,
        entry);
}

u32 LoadTrcRecord9Value() {
    std::array<u8, 64> record{};
    std::size_t bytes_read = 0;
    if (!read_trc_record_bytes("JW2_01.TRC", 9, record.data(), record.size(),
            &bytes_read) ||
        bytes_read <= 0x20) {
        return 0;
    }

    // The original 0x00407d60 does not return the first dword of version.dat.
    // It gathers the year from bytes 0x0e..0x0f and the month/day from bytes
    // 0x1f..0x20 into the packed little-endian value YYYY-MM-DD.
    return static_cast<u32>(record[0x0e]) |
        (static_cast<u32>(record[0x0f]) << 8) |
        (static_cast<u32>(record[0x1f]) << 16) |
        (static_cast<u32>(record[0x20]) << 24);
}

bool BuildTrcRecord10Key(char (&out_key)[16]) {
    std::array<u8, 128> record{};
    std::size_t bytes_read = 0;
    if (!read_trc_record_bytes("JW2_01.TRC", 10, record.data(), record.size(),
            &bytes_read) ||
        bytes_read <= 0x76) {
        std::memset(out_key, 0, sizeof(out_key));
        return false;
    }

    out_key[0] = static_cast<char>(record[0x3d] ^ 0x40);
    out_key[1] = static_cast<char>(record[0x15] ^ 0x0c);
    out_key[2] = static_cast<char>(record[0x2f] ^ 0x07);
    out_key[3] = static_cast<char>(record[0x6f] ^ 0x9d);
    out_key[4] = '-';
    out_key[5] = static_cast<char>(record[0x03] ^ 0xfe);
    out_key[6] = static_cast<char>(record[0x06] ^ 0xea);
    out_key[7] = static_cast<char>(record[0x27] ^ 0xc1);
    out_key[8] = static_cast<char>(record[0x07] ^ 0x42);
    out_key[9] = '-';
    out_key[10] = static_cast<char>(record[0x76] ^ 0x4d);
    out_key[11] = static_cast<char>(record[0x42] ^ 0x22);
    out_key[12] = static_cast<char>(record[0x4e] ^ 0x1a);
    out_key[13] = static_cast<char>(record[0x45] ^ 0x3b);
    out_key[14] = static_cast<char>(record[0x62] ^ 0x50);
    out_key[15] = '\0';
    ascii_upper_in_place(out_key);
    return true;
}

void SwapDecimalDigit(u32& lhs, u32 lhs_position, u32& rhs, u32 rhs_position) {
    swap_decimal_digit(lhs, lhs_position, rhs, rhs_position);
}

void SwapBitBetweenValues(u32& lhs, u32 lhs_bit, u32& rhs, u32 rhs_bit) {
    swap_bit(lhs, static_cast<u8>(lhs_bit), rhs, static_cast<u8>(rhs_bit));
}

bool ValidateEncodedTrcKey(char (&group1)[5], char (&group2)[5],
    char (&group3)[6]) {
    return validate_encoded_trc_key(group1, group2, group3);
}

bool ValidateTrcRecord10Key() {
    char key[16]{};
    if (!BuildTrcRecord10Key(key)) {
        return false;
    }

    char group1[5]{key[0], key[1], key[2], key[3], '\0'};
    char group2[5]{key[5], key[6], key[7], key[8], '\0'};
    char group3[6]{key[10], key[11], key[12], key[13], key[14], '\0'};
    return ValidateEncodedTrcKey(group1, group2, group3);
}

u32 PassThroughTrcKeyValidationValue(u32 value) {
    return value;
}

u32 QueryTrcRecordOriginalSize(const char* archive_name, u32 record_index) {
    u32 original_size = 0;
    if (!QueryTrcRecordSizes(archive_name, record_index, nullptr, &original_size, nullptr)) {
        return 0xffffffffu;
    }
    return original_size;
}

bool QueryTrcRecordSizes(const char* archive_name, u32 record_index, u32* stored_size,
    u32* original_size, TrcDirectoryEntry* entry_out) {
    std::vector<u8> data;
    TrcDirectoryEntry entry;
    u32 data_offset = 0;
    if (!read_archive_and_directory_entry(archive_name, record_index, data, entry, data_offset)) {
        return false;
    }

    if (original_size != nullptr) {
        *original_size = entry.original_size;
    }
    if (stored_size != nullptr) {
        *stored_size = entry.stored_size;
    }
    if (entry_out != nullptr) {
        *entry_out = entry;
    }
    return true;
}

bool ExtractTrcRecordToFile(const char* archive_name, u32 record_index,
    const char* destination_path) {
    if (destination_path == nullptr) {
        return false;
    }

    std::vector<u8> record;
    if (!read_trc_record(archive_name, record_index, record, nullptr)) {
        return false;
    }

    std::ofstream output(destination_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    if (!record.empty()) {
        output.write(reinterpret_cast<const char*>(record.data()),
            static_cast<std::streamsize>(record.size()));
    }
    output.flush();
    if (!output) {
        return false;
    }
    output.close();
    if (!output) {
        return false;
    }
    invalidate_archive_read_cache_after_write();
    return true;
}

bool QueryTrcArchiveRecordCount(const char* archive_name, u32* active_records,
    u32* directory_slots) {
    if (active_records != nullptr) {
        *active_records = 0;
    }
    if (directory_slots != nullptr) {
        *directory_slots = 0;
    }
    if (archive_name == nullptr) {
        return false;
    }

    std::vector<TrcDirectoryEntry> entries;
    u32 local_directory_slots = 0;
    if (!read_archive_record_manifest(archive_name, entries, local_directory_slots)) {
        return false;
    }

    if (active_records != nullptr) {
        *active_records = static_cast<u32>(entries.size());
    }
    if (directory_slots != nullptr) {
        *directory_slots = local_directory_slots;
    }
    return true;
}

bool LoadTrcRecordAlloc(const char* archive_name, u32 record_index, std::vector<u8>& out,
    std::size_t extra_bytes, TrcDirectoryEntry* entry) {
    out.clear();

    auto return_legacy_null_fallback = [&out, extra_bytes]() {
        if (extra_bytes == 0) {
            return false;
        }
        out.assign(kLegacyTrcNullFallback.begin(), kLegacyTrcNullFallback.end());
        return true;
    };

    TrcRecordReader reader;
    if (!OpenTrcRecordDirectoryEntry(reader, archive_name, record_index) ||
        !OpenTrcRecordPayload(reader)) {
        CloseTrcRecordReader(reader);
        return return_legacy_null_fallback();
    }
    if (extra_bytes > static_cast<std::size_t>(0xffffffffu) ||
        reader.entry.original_size > std::numeric_limits<std::size_t>::max() - extra_bytes) {
        CloseTrcRecordReader(reader);
        return return_legacy_null_fallback();
    }

    const TrcDirectoryEntry local_entry = reader.entry;
    const std::size_t original_size = local_entry.original_size;
    out.assign(original_size + extra_bytes, 0);
    if (extra_bytes == 1) {
        out[original_size] = 0;
    }
    const bool read_ok = ReadOpenTrcRecordBytes(reader, out.data(), original_size);
    if (entry != nullptr) {
        *entry = local_entry;
    }
    CloseTrcRecordReader(reader);
    if (!read_ok) {
        out.clear();
        return false;
    }
    return true;
}

bool HandleTrcRecordRangeReplacement(const char* destination_archive, u32 destination_start,
    const char* source_archive, u32 source_start, u32 record_count) {
    if (destination_archive == nullptr || source_archive == nullptr) {
        return false;
    }
    if (record_count == 0) {
        return true;
    }

    std::vector<TrcWriteRecord> destination_records;
    std::vector<TrcWriteRecord> source_records;
    u32 destination_slots = 0;
    u32 source_slots = 0;
    if (!read_trc_write_records(destination_archive, destination_records, destination_slots) ||
        !read_trc_write_records(source_archive, source_records, source_slots)) {
        return false;
    }

    const u64 destination_end = static_cast<u64>(destination_start) + record_count;
    const u64 source_end = static_cast<u64>(source_start) + record_count;
    if (destination_end > destination_records.size() || source_end > source_records.size()) {
        return false;
    }

    for (u32 i = 0; i < record_count; ++i) {
        destination_records[destination_start + i] = source_records[source_start + i];
    }

    return WriteTrcRecords(destination_archive, destination_records, destination_slots);
}

bool ReplaceTrcRecordPayloadFromFile(const char* archive_name, u32 record_index,
    const char* file_path) {
    if (archive_name == nullptr || file_path == nullptr) {
        return false;
    }

    std::vector<TrcWriteRecord> records;
    u32 directory_slots = 0;
    if (!read_trc_write_records(archive_name, records, directory_slots) ||
        record_index >= records.size()) {
        return false;
    }

    const auto source_path = find_archive_path(file_path);
    if (!source_path || !read_all_bytes(*source_path, records[record_index].payload)) {
        return false;
    }
    records[record_index].has_stored_payload = false;
    records[record_index].has_original_size = false;
    records[record_index].has_check_value = false;

    return WriteTrcRecords(archive_name, records, directory_slots);
}

bool PatchTrcRecordPayloadFromMemory(const char* archive_name, u32 record_index,
    const void* payload, std::size_t payload_size, u32 destination_offset) {
    if (archive_name == nullptr || (payload == nullptr && payload_size != 0) ||
        payload_size > static_cast<std::size_t>(0xffffffffu)) {
        return false;
    }

    std::vector<TrcWriteRecord> records;
    u32 directory_slots = 0;
    if (!read_trc_write_records(archive_name, records, directory_slots) ||
        record_index >= records.size()) {
        return false;
    }

    TrcWriteRecord& record = records[record_index];
    const std::size_t offset = destination_offset;
    if (offset > record.payload.size() ||
        payload_size > record.payload.size() - offset) {
        return false;
    }

    if (payload_size != 0) {
        const auto* bytes = static_cast<const u8*>(payload);
        std::copy(bytes, bytes + payload_size, record.payload.begin() + offset);
        record.has_stored_payload = false;
        record.has_original_size = false;
    }
    return WriteTrcRecords(archive_name, records, directory_slots);
}

bool PatchTrcRecordPayloadFromFile(const char* archive_name, u32 record_index,
    const char* file_path, u32 destination_offset) {
    if (file_path == nullptr) {
        return false;
    }

    const auto source_path = find_archive_path(file_path);
    std::vector<u8> payload;
    if (!source_path || !read_all_bytes(*source_path, payload)) {
        return false;
    }
    return PatchTrcRecordPayloadFromMemory(archive_name, record_index, payload.data(),
        payload.size(), destination_offset);
}

bool WriteEmptyTrcBuilderHeader(const char* archive_name, u32 directory_slots) {
    if (archive_name == nullptr) {
        return false;
    }
    if (directory_slots == 0) {
        directory_slots = kTrcBuilderDirectoryGrowth;
    }

    const std::size_t data_offset =
        kTrcHeaderSize + static_cast<std::size_t>(directory_slots) * kTrcEntrySize;
    if (data_offset > static_cast<std::size_t>(0xffffffffu)) {
        return false;
    }

    std::ofstream output(archive_name, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    std::array<u8, kTrcHeaderSize> header{};
    std::copy(kTrcMagic.begin(), kTrcMagic.end(), header.begin());
    write_le_u32(header.data() + 0x04, directory_slots);
    write_le_u32(header.data() + 0x08, 0);
    write_le_u32(header.data() + 0x0c, static_cast<u32>(data_offset));
    output.write(reinterpret_cast<const char*>(header.data()), header.size());

    std::array<u8, kTrcEntrySize> empty_entry{};
    for (u32 slot = 0; slot < directory_slots; ++slot) {
        output.write(reinterpret_cast<const char*>(empty_entry.data()), empty_entry.size());
    }
    output.flush();
    if (!output) {
        return false;
    }
    output.close();
    if (!output) {
        return false;
    }
    invalidate_archive_read_cache_after_write();
    return true;
}

bool CheckTrcBuilderDirectorySlots(TrcOutputBuilder& builder, u32 required_records) {
    if (builder.directory_growth == 0) {
        builder.directory_growth = kTrcBuilderDirectoryGrowth;
    }
    while (builder.directory_slots < required_records) {
        if (builder.directory_slots >
            std::numeric_limits<u32>::max() - builder.directory_growth) {
            return false;
        }
        if (!SetTrcBuilderDirectoryCapacity(builder,
                builder.directory_slots + builder.directory_growth)) {
            return false;
        }
    }
    return true;
}

bool SetTrcBuilderDirectoryCapacity(TrcOutputBuilder& builder, u32 directory_slots) {
    if (directory_slots < builder.records.size()) {
        return false;
    }
    if (directory_slots > builder.directory_slots) {
        builder.directory_slots = directory_slots;
    }
    return true;
}

bool OpenTrcBuilderInputFile(TrcBuilderInputFile& input, const char* file_path) {
    HandleTrcBuilderInputHandleRelease(input);
    if (file_path == nullptr) {
        return false;
    }

    const auto payload_path = find_archive_path(file_path);
    if (!payload_path || !read_all_bytes(*payload_path, input.payload)) {
        HandleTrcBuilderInputHandleRelease(input);
        return false;
    }

    input.path = file_path;
    input.open = true;
    return true;
}

void HandleTrcBuilderInputHandleRelease(TrcBuilderInputFile& input) {
    input = TrcBuilderInputFile{};
}

bool InitializeTrcBuilderMemoryRecord(TrcOutputBuilder& builder, const char* archive_name,
    const char* record_name, u32 directory_growth, u16 storage_method) {
    HandleTrcOutputBuilderRelease(builder);
    const std::string trc_record_name = make_trc_record_name(record_name);
    if (archive_name == nullptr || trc_record_name.empty()) {
        return false;
    }

    builder.archive_name = archive_name;
    builder.directory_growth = directory_growth == 0 ? kTrcBuilderDirectoryGrowth :
        directory_growth;
    if (!load_trc_records_for_append(archive_name, builder.records, builder.directory_slots)) {
        HandleTrcOutputBuilderRelease(builder);
        return false;
    }
    if (!CheckTrcBuilderDirectorySlots(builder,
            static_cast<u32>(builder.records.size() + 1))) {
        HandleTrcOutputBuilderRelease(builder);
        return false;
    }
    if (builder.records.empty() &&
        !WriteEmptyTrcBuilderHeader(builder.archive_name.c_str(), builder.directory_slots)) {
        HandleTrcOutputBuilderRelease(builder);
        return false;
    }

    builder.active_record.name = trc_record_name;
    builder.active_record.method = storage_method == 2 ? 2 : 0;
    builder.active = true;
    builder.record_initialized = true;
    return true;
}

bool HandleTrcBuilderPayloadWrite(TrcOutputBuilder& builder, const void* payload,
    std::size_t payload_size) {
    if (!builder.active || !builder.record_initialized ||
        (payload == nullptr && payload_size != 0) ||
        payload_size > static_cast<std::size_t>(0xffffffffu)) {
        return false;
    }

    builder.active_record.payload.clear();
    if (payload_size != 0) {
        const auto* bytes = static_cast<const u8*>(payload);
        builder.active_record.payload.assign(bytes, bytes + payload_size);
    }
    builder.active_record.check_value = payload_size == 0 ? 0 :
        signed_byte_sum_checksum(static_cast<const u8*>(payload), payload_size);
    builder.active_record.has_check_value = true;
    builder.payload_written = true;
    return true;
}

bool HandleTrcBuilderRecordCommit(TrcOutputBuilder& builder) {
    if (!builder.active || !builder.record_initialized || !builder.payload_written) {
        return false;
    }

    builder.records.push_back(std::move(builder.active_record));
    const bool ok = WriteTrcRecords(builder.archive_name.c_str(), builder.records,
        builder.directory_slots);
    if (ok) {
        builder.active_record = TrcWriteRecord{};
        builder.record_initialized = false;
        builder.payload_written = false;
    }
    return ok;
}

void HandleTrcOutputBuilderRelease(TrcOutputBuilder& builder) {
    builder = TrcOutputBuilder{};
}

bool AppendFilePayloadToTrcBuilder(const char* archive_name, const char* file_path,
    u16 storage_method) {
    const std::string record_name = make_trc_record_name(file_path);
    if (archive_name == nullptr || file_path == nullptr || record_name.empty()) {
        return false;
    }

    TrcBuilderInputFile input;
    if (!OpenTrcBuilderInputFile(input, file_path)) {
        return false;
    }

    TrcOutputBuilder builder;
    bool ok = InitializeTrcBuilderMemoryRecord(builder, archive_name,
            record_name.c_str(), kTrcBuilderDirectoryGrowth, storage_method) &&
        HandleTrcBuilderPayloadWrite(builder, input.payload.data(), input.payload.size());
    if (ok && storage_method < 2) {
        builder.active_record.check_value = 0;
        builder.active_record.has_check_value = true;
    }
    ok = ok && HandleTrcBuilderRecordCommit(builder);
    HandleTrcOutputBuilderRelease(builder);
    HandleTrcBuilderInputHandleRelease(input);
    return ok;
}

bool AppendArchiveRecordToTrcBuilder(const char* destination_archive,
    const char* source_archive, u32 source_record_index, u16 storage_method) {
    if (destination_archive == nullptr || source_archive == nullptr) {
        return false;
    }

    std::vector<u8> payload;
    TrcDirectoryEntry entry;
    if (!read_trc_record(source_archive, source_record_index, payload, &entry)) {
        return false;
    }

    const std::string record_name = make_trc_record_name(source_archive);
    TrcOutputBuilder builder;
    bool ok = InitializeTrcBuilderMemoryRecord(builder, destination_archive,
        record_name.c_str(), kTrcBuilderDirectoryGrowth, storage_method);
    if (ok && storage_method == 1) {
        ok = false;
    }
    else if (ok && storage_method == 2 && entry.method == 2) {
        std::vector<u8> stored_payload;
        TrcDirectoryEntry stored_entry;
        ok = read_trc_record_stored_bytes(source_archive, source_record_index,
            stored_payload, &stored_entry);
        if (ok) {
            builder.active_record.payload.clear();
            builder.active_record.stored_payload = std::move(stored_payload);
            builder.active_record.has_stored_payload = true;
            builder.active_record.original_size = stored_entry.original_size;
            builder.active_record.has_original_size = true;
            builder.active_record.check_value = stored_entry.check_value;
            builder.active_record.has_check_value = true;
            builder.payload_written = true;
        }
    }
    else {
        ok = ok && HandleTrcBuilderPayloadWrite(builder, payload.data(), payload.size());
        if (ok && storage_method == 0 && entry.method == 0) {
            builder.active_record.check_value = 0;
            builder.active_record.has_check_value = true;
        }
    }
    const bool committed = ok && HandleTrcBuilderRecordCommit(builder);
    HandleTrcOutputBuilderRelease(builder);
    return committed;
}

bool HandleTrcMemoryRecordAppend(const char* archive_name, const char* record_name,
    const void* payload, std::size_t payload_size, u32 directory_growth, u16 storage_method) {
    const std::string trc_record_name = make_trc_record_name(record_name);
    if (archive_name == nullptr || trc_record_name.empty() ||
        (payload == nullptr && payload_size != 0) ||
        payload_size > static_cast<std::size_t>(0xffffffffu)) {
        return false;
    }

    TrcOutputBuilder builder;
    const bool ok = InitializeTrcBuilderMemoryRecord(builder, archive_name,
            trc_record_name.c_str(), directory_growth, storage_method) &&
        HandleTrcBuilderPayloadWrite(builder, payload, payload_size) &&
        HandleTrcBuilderRecordCommit(builder);
    HandleTrcOutputBuilderRelease(builder);
    return ok;
}

bool CopyTrcBytesWithOptionalChecksum(std::istream& input, std::ostream& output,
    std::size_t byte_count, u16* checksum) {
    constexpr std::size_t kCopyChunkSize = 0x100000;
    std::vector<char> buffer(std::min(byte_count, kCopyChunkSize));
    if (byte_count != 0 && buffer.empty()) {
        return false;
    }

    while (byte_count != 0) {
        const std::size_t chunk_size = std::min(byte_count, buffer.size());
        input.read(buffer.data(), static_cast<std::streamsize>(chunk_size));
        if (static_cast<std::size_t>(input.gcount()) != chunk_size) {
            return false;
        }

        output.write(buffer.data(), static_cast<std::streamsize>(chunk_size));
        if (!output) {
            return false;
        }

        if (checksum != nullptr) {
            for (std::size_t i = 0; i < chunk_size; ++i) {
                *checksum = static_cast<u16>(
                    *checksum + static_cast<u8>(static_cast<unsigned char>(buffer[i])));
            }
        }

        byte_count -= chunk_size;
    }

    return true;
}

bool WriteTrcRecords(const char* archive_name, const std::vector<TrcWriteRecord>& records,
    u32 directory_slots) {
    if (archive_name == nullptr || records.empty()) {
        return false;
    }
    if (records.size() > static_cast<std::size_t>(0xffffffffu)) {
        return false;
    }

    const u32 active_records = static_cast<u32>(records.size());
    if (directory_slots < active_records) {
        directory_slots = active_records;
    }

    std::vector<PreparedTrcWriteRecord> prepared;
    if (!prepare_trc_write_records(records, prepared)) {
        return false;
    }

    const std::size_t data_offset =
        kTrcHeaderSize + static_cast<std::size_t>(directory_slots) * kTrcEntrySize;
    if (data_offset > static_cast<std::size_t>(0xffffffffu)) {
        return false;
    }

    std::ofstream output(archive_name, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    std::array<u8, kTrcHeaderSize> header{};
    std::copy(kTrcMagic.begin(), kTrcMagic.end(), header.begin());
    write_le_u32(header.data() + 0x04, directory_slots);
    write_le_u32(header.data() + 0x08, active_records);
    write_le_u32(header.data() + 0x0c, static_cast<u32>(data_offset));
    output.write(reinterpret_cast<const char*>(header.data()), header.size());

    u32 relative_offset = 0;
    for (const PreparedTrcWriteRecord& record : prepared) {
        std::array<u8, kTrcEntrySize> entry{};
        const std::size_t name_bytes = std::min<std::size_t>(record.name.size(), 12);
        std::memcpy(entry.data(), record.name.data(), name_bytes);
        write_le_u32(entry.data() + 0x0c, relative_offset);
        write_le_u32(entry.data() + 0x10, record.original_size);
        write_le_u32(entry.data() + 0x14, static_cast<u32>(record.stored_payload.size()));
        write_le_u16(entry.data() + 0x18, record.check_value);
        write_le_u16(entry.data() + 0x1a, record.method);
        write_le_u32(entry.data() + 0x1c, record.reserved);
        output.write(reinterpret_cast<const char*>(entry.data()), entry.size());
        relative_offset += static_cast<u32>(record.stored_payload.size());
    }

    std::array<u8, kTrcEntrySize> empty_entry{};
    for (u32 slot = active_records; slot < directory_slots; ++slot) {
        output.write(reinterpret_cast<const char*>(empty_entry.data()), empty_entry.size());
    }

    for (const PreparedTrcWriteRecord& record : prepared) {
        if (!record.stored_payload.empty()) {
            output.write(reinterpret_cast<const char*>(record.stored_payload.data()),
                static_cast<std::streamsize>(record.stored_payload.size()));
        }
    }

    output.flush();
    if (!output) {
        return false;
    }
    output.close();
    if (!output) {
        return false;
    }
    invalidate_archive_read_cache_after_write();
    return true;
}

}
