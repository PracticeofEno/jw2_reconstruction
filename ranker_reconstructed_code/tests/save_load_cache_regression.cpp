#include "ranker_trc.h"
#include "ranker_resource_store.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

ranker::TrcWriteRecord make_record(const char* text) {
    ranker::TrcWriteRecord record{};
    record.name = "STATE";
    record.payload.assign(text, text + std::char_traits<char>::length(text));
    record.method = 0;
    return record;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace ranker;

    const fs::path directory = fs::temp_directory_path() /
        ("ranker_save_load_cache_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path archive = directory / "_Jw2_00.sav";
    std::error_code ec;
    if (!fs::create_directories(directory, ec) || ec) {
        return 1;
    }

    const std::string archive_name = archive.string();
    if (!WriteTrcRecords(archive_name.c_str(), {make_record("firstgame")}, 1)) {
        return 2;
    }

    std::vector<u8> loaded;
    if (!LoadTrcRecordAlloc(archive_name.c_str(), 0, loaded) ||
        std::string(loaded.begin(), loaded.end()) != "firstgame") {
        return 3;
    }
    const auto original_time = fs::last_write_time(archive, ec);
    if (ec) {
        return 4;
    }
    const auto original_size = fs::file_size(archive, ec);
    if (ec) {
        return 5;
    }

    // Keep both cache validators identical.  A write-side invalidation is the
    // only reliable way to distinguish the replacement archive in this case.
    if (!WriteTrcRecords(archive_name.c_str(), {make_record("othergame")}, 1)) {
        return 6;
    }
    if (fs::file_size(archive, ec) != original_size || ec) {
        return 7;
    }
    fs::last_write_time(archive, original_time, ec);
    if (ec) {
        return 8;
    }

    loaded.clear();
    if (!LoadTrcRecordAlloc(archive_name.c_str(), 0, loaded) ||
        std::string(loaded.begin(), loaded.end()) != "othergame") {
        return 9;
    }

    fs::remove_all(directory, ec);
    if (ec) {
        return 10;
    }

    // UI and gameplay resources share the original stack allocator.  A
    // parent menu released after a child/session import necessarily removes
    // that later import too, and the stale indices can immediately be reused
    // for unrelated pixels.
    ResetResourceStore();
    const u32 parent_mark = resource_store_state().next_entry;
    u32 parent_resource = kInvalidResourceEntry;
    u32 imported_resource = kInvalidResourceEntry;
    if (!AllocateResourceEntry(4, &parent_resource, nullptr) ||
        !AllocateResourceEntry(8, &imported_resource, nullptr)) {
        return 11;
    }
    const u64 stale_import_serial =
        GetResourceEntryAllocationSerial(imported_resource);
    ReleaseResourceEntriesFrom(parent_mark);
    if (GetResourceEntry(imported_resource) != nullptr) {
        return 12;
    }
    u32 reused_parent_resource = kInvalidResourceEntry;
    u32 reused_import_resource = kInvalidResourceEntry;
    if (!AllocateResourceEntry(16, &reused_parent_resource, nullptr) ||
        !AllocateResourceEntry(16, &reused_import_resource, nullptr) ||
        reused_parent_resource != parent_mark ||
        reused_import_resource != imported_resource ||
        GetResourceEntryAllocationSerial(reused_import_resource) ==
            stale_import_serial) {
        return 13;
    }

    // Fixed frontend order: close the parent stack frame first, then import
    // the saved game's resources.  No later parent release can truncate them.
    ResetResourceStore();
    const u32 fixed_parent_mark = resource_store_state().next_entry;
    if (!AllocateResourceEntry(4, &parent_resource, nullptr)) {
        return 14;
    }
    ReleaseResourceEntriesFrom(fixed_parent_mark);
    if (!AllocateResourceEntry(8, &imported_resource, nullptr) ||
        GetResourceEntry(imported_resource) == nullptr) {
        return 15;
    }
    return 0;
}
