#include "ranker_text_tables.h"

#include "ranker_indexed_text_table.h"
#include "ranker_trc.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <new>
#include <string>
#include <utility>

namespace ranker {
namespace {

StartupTextTables g_startup_text_tables;

bool is_line_delimiter(char value) {
    return value == '\r' || value == '\n';
}

void split_crlf_tokens(TextTable& table) {
    table.rows.clear();
    if (table.storage.empty()) {
        return;
    }

    char* cursor = table.storage.data();
    while (*cursor != '\0') {
        while (is_line_delimiter(*cursor)) {
            *cursor = '\0';
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        char* begin = cursor;
        while (*cursor != '\0' && !is_line_delimiter(*cursor)) {
            ++cursor;
        }

        table.rows.emplace_back(begin, static_cast<std::size_t>(cursor - begin));
        while (is_line_delimiter(*cursor)) {
            *cursor = '\0';
            ++cursor;
        }
    }
}

bool load_text_table(const char* archive_name, u32 record_index, TextTable& table,
    bool skip_if_loaded) {
    if (skip_if_loaded && !table.storage.empty()) {
        return true;
    }

    u32 original_size = 0;
    if (!QueryTrcRecordSizes(archive_name, record_index, nullptr, &original_size)) {
        return false;
    }

    table.storage.clear();
    table.rows.clear();

    std::vector<char> storage;
    try {
        storage.assign(static_cast<std::size_t>(original_size) + 1u, '\0');
    } catch (const std::bad_alloc&) {
        return false;
    }

    if (!LoadTrcRecordIntoBuffer(archive_name, record_index, storage.data(),
            storage.size())) {
        table.storage.clear();
        table.rows.clear();
        return false;
    }

    table.storage = std::move(storage);
    split_crlf_tokens(table);
    return true;
}

bool equals_ignore_case_c_string(const std::vector<u8>& record, const char* expected) {
    if (record.empty() || expected == nullptr) {
        return false;
    }
    const char* value = reinterpret_cast<const char*>(record.data());
    std::size_t index = 0;
    while (value[index] != '\0' && expected[index] != '\0') {
        const int left = std::tolower(static_cast<unsigned char>(value[index]));
        const int right = std::tolower(static_cast<unsigned char>(expected[index]));
        if (left != right) {
            return false;
        }
        ++index;
    }
    return value[index] == '\0' && expected[index] == '\0';
}

bool load_auxiliary_startup_records(const char* archive_name) {
    constexpr std::array<u32, 7> kAuxiliaryRecordIndices{2, 3, 4, 5, 6, 7, 8};

    std::vector<TextTable> loaded(kAuxiliaryRecordIndices.size());
    for (std::size_t slot = 0; slot < kAuxiliaryRecordIndices.size(); ++slot) {
        const u32 record_index = kAuxiliaryRecordIndices[slot];
        std::vector<u8> record;
        if (!LoadTrcRecordAlloc(archive_name, record_index, record, 1)) {
            return false;
        }

        if (equals_ignore_case_c_string(record, "NULL")) {
            ResetIndexedTextTableContext(
                StartupAuxiliaryIndexedTextTable(static_cast<u32>(slot)));
            g_startup_text_tables.auxiliary_rows = std::move(loaded);
            return true;
        }
        if (!LoadIndexedTextTableFromMemory(
                StartupAuxiliaryIndexedTextTable(static_cast<u32>(slot)),
                reinterpret_cast<const char*>(record.data()))) {
            g_startup_text_tables.auxiliary_rows = std::move(loaded);
            return false;
        }

        TextTable table;
        table.storage.assign(record.begin(), record.end());
        split_crlf_tokens(table);
        loaded[slot] = std::move(table);
    }

    g_startup_text_tables.auxiliary_rows = std::move(loaded);
    return true;
}

} // namespace

bool LoadStartupPlatformTextTables(const char* archive_name, u32 record_index) {
    if (!load_text_table(archive_name, record_index,
            g_startup_text_tables.platform_rows, false)) {
        g_startup_text_tables.loaded = false;
        return false;
    }

    if (!load_auxiliary_startup_records(archive_name)) {
        g_startup_text_tables.loaded = false;
        return false;
    }

    g_startup_text_tables.loaded = !g_startup_text_tables.message_rows.rows.empty();
    return true;
}

bool LoadStartupMessageTextTable(const char* archive_name, u32 record_index) {
    if (!load_text_table(archive_name, record_index,
            g_startup_text_tables.message_rows, true)) {
        g_startup_text_tables.loaded = false;
        return false;
    }

    g_startup_text_tables.loaded = !g_startup_text_tables.platform_rows.rows.empty();
    return true;
}

bool LoadStartupJw217TextTables() {
    const bool platform_ok = LoadStartupPlatformTextTables("JW2_17.TRC", 0);
    const bool messages_ok = LoadStartupMessageTextTable("JW2_17.TRC", 1);
    g_startup_text_tables.loaded = platform_ok && messages_ok;
    return g_startup_text_tables.loaded;
}

void ReleaseStartupPlatformTextTables() {
    g_startup_text_tables.platform_rows.storage.clear();
    g_startup_text_tables.platform_rows.rows.clear();
    g_startup_text_tables.auxiliary_rows.clear();
    for (u32 slot = 0; slot < 7; ++slot) {
        ResetIndexedTextTableContext(StartupAuxiliaryIndexedTextTable(slot));
    }
    g_startup_text_tables.loaded = false;
}

void ReleaseStartupMessageTextTable() {
    g_startup_text_tables.message_rows.storage.clear();
    g_startup_text_tables.message_rows.rows.clear();
    g_startup_text_tables.loaded = false;
}

const StartupTextTables& startup_text_tables() {
    return g_startup_text_tables;
}

const char* startup_platform_row(std::size_t index, const char* fallback) {
    const auto& rows = g_startup_text_tables.platform_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = g_startup_text_tables.message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

std::size_t startup_platform_text_count() {
    return g_startup_text_tables.platform_rows.rows.size();
}

std::size_t startup_message_text_count() {
    return g_startup_text_tables.message_rows.rows.size();
}

}
