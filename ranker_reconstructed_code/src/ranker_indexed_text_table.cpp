#include "ranker_indexed_text_table.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ranker {
namespace {

constexpr std::size_t kStartupAuxiliaryTableCount = 7;

std::array<IndexedTextTableContext, kStartupAuxiliaryTableCount> g_startup_auxiliary_tables;

std::string trim_copy(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

bool parse_indexed_text_line(std::string_view line, u32& index, std::string& value) {
    line = std::string_view(trim_copy(line));
    if (line.empty() || line.front() == ';') {
        return false;
    }

    std::size_t cursor = 0;
    while (cursor < line.size() && std::isdigit(static_cast<unsigned char>(line[cursor])) != 0) {
        ++cursor;
    }
    if (cursor == 0) {
        return false;
    }

    index = static_cast<u32>(std::strtoul(std::string(line.substr(0, cursor)).c_str(),
        nullptr, 10));

    while (cursor < line.size() &&
        (std::isspace(static_cast<unsigned char>(line[cursor])) != 0 ||
            line[cursor] == '=' || line[cursor] == ',')) {
        ++cursor;
    }

    if (cursor < line.size() && line[cursor] == '"') {
        const std::size_t begin = ++cursor;
        while (cursor < line.size() && line[cursor] != '"') {
            ++cursor;
        }
        value = std::string(line.substr(begin, cursor - begin));
    }
    else {
        value = trim_copy(line.substr(cursor));
    }
    return true;
}

void load_source_lines(const std::string& source, std::vector<std::string_view>& lines) {
    lines.clear();
    std::size_t begin = 0;
    while (begin < source.size()) {
        std::size_t end = begin;
        while (end < source.size() && source[end] != '\r' && source[end] != '\n') {
            ++end;
        }
        lines.emplace_back(source.data() + begin, end - begin);
        while (end < source.size() && (source[end] == '\r' || source[end] == '\n')) {
            ++end;
        }
        begin = end;
    }
}

bool read_file_to_string(const char* path, std::string& out) {
    if (path == nullptr) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

void register_startup_destructor(IndexedTextTableContext& table, void (*callback)()) {
    if (!table.destructor_registered) {
        table.destructor_registered = true;
        std::atexit(callback);
    }
}

} // namespace

void InitializeIndexedTextTableContext(IndexedTextTableContext& table) {
    InitializeIndexedTextTableReader(table);
    table.delimiters = {"\"", "\n"};
    table.rows.clear();
    table.empty_row.clear();
    table.cursor = 0;
}

void DestroyIndexedTextTableContext(IndexedTextTableContext& table) {
    ResetIndexedTextTableContext(table);
    DestroyIndexedTextTableReader(table);
}

void ResetIndexedTextTableContext(IndexedTextTableContext& table) {
    ReleaseIndexedTextTableRows(table);
    ReleaseIndexedTextTableSource(table);
}

bool BuildIndexedTextTableRows(IndexedTextTableContext& table) {
    if (!table.source_loaded && !RewindIndexedTextTableSource(table)) {
        return false;
    }

    std::vector<std::string_view> lines;
    load_source_lines(table.source_text, lines);

    u32 max_index = 0;
    bool any = false;
    for (std::string_view line : lines) {
        u32 index = 0;
        std::string value;
        if (parse_indexed_text_line(line, index, value)) {
            max_index = std::max(max_index, index);
            any = true;
        }
    }
    if (!any) {
        table.rows.clear();
        return false;
    }

    table.rows.assign(static_cast<std::size_t>(max_index) + 1, table.empty_row);
    for (std::string_view line : lines) {
        u32 index = 0;
        std::string value;
        if (parse_indexed_text_line(line, index, value) && index < table.rows.size()) {
            table.rows[index] = std::move(value);
        }
    }
    return true;
}

const char* FindLastIndexedTextTableCharacter(const char* text, char value) {
    return std::strrchr(text, value);
}

bool LoadIndexedTextTableFromFile(IndexedTextTableContext& table, const char* path) {
    ResetIndexedTextTableContext(table);
    if (!LoadIndexedTextTableFileSource(table, path)) {
        return false;
    }
    return BuildIndexedTextTableRows(table);
}

bool LoadIndexedTextTableFromMemory(IndexedTextTableContext& table, const char* text) {
    ResetIndexedTextTableContext(table);
    if (!LoadIndexedTextTableMemorySource(table, text)) {
        return false;
    }
    return BuildIndexedTextTableRows(table);
}

std::string_view GetIndexedTextTableRow(const IndexedTextTableContext& table, u32 index) {
    if (index < table.rows.size()) {
        return table.rows[index];
    }
    return table.empty_row;
}

void SetIndexedTextTableRow(IndexedTextTableContext& table, u32 index, const char* text) {
    if (index >= table.rows.size()) {
        return;
    }

    ReleaseIndexedTextTableRow(table, index);
    if (text != nullptr) {
        table.rows[index] = text;
    }
}

void ReleaseIndexedTextTableRow(IndexedTextTableContext& table, u32 index) {
    if (index < table.rows.size()) {
        table.rows[index] = table.empty_row;
    }
}

void ReleaseIndexedTextTableRows(IndexedTextTableContext& table) {
    table.rows.clear();
}

void InitializeIndexedTextTableReader(IndexedTextTableContext& table) {
    InitializeDpgArchiveContext(table.archive);
    table.source_loaded = false;
    table.memory_source = false;
    table.reload_pending = true;
    table.source_name.clear();
    table.source_text.clear();
    table.original_memory_source.clear();
    table.cursor = 0;
}

void DestroyIndexedTextTableReader(IndexedTextTableContext& table) {
    ReleaseIndexedTextTableSource(table);
    CloseDpgArchiveWrapper(table.archive);
}

bool OpenIndexedTextTableArchive(IndexedTextTableContext& table, const char* archive_path) {
    CloseIndexedTextTableArchive(table);
    return OpenDpgArchive(table.archive, archive_path, false);
}

void CloseIndexedTextTableArchive(IndexedTextTableContext& table) {
    CloseDpgArchive(table.archive);
}

bool LoadIndexedTextTableFileSource(IndexedTextTableContext& table, const char* path) {
    ReleaseIndexedTextTableSource(table);
    if (!read_file_to_string(path, table.source_text)) {
        return false;
    }
    table.source_loaded = true;
    table.memory_source = false;
    table.reload_pending = true;
    table.cursor = 0;
    return true;
}

bool LoadIndexedTextTableMemorySource(IndexedTextTableContext& table, const char* text) {
    ReleaseIndexedTextTableSource(table);
    if (text[0] == '\0') {
        return false;
    }
    table.original_memory_source = text;
    table.source_text = text;
    table.source_loaded = true;
    table.memory_source = true;
    table.reload_pending = true;
    table.cursor = 0;
    return true;
}

void ReleaseIndexedTextTableSource(IndexedTextTableContext& table) {
    table.source_text.clear();
    table.original_memory_source.clear();
    table.source_loaded = false;
    table.memory_source = false;
    table.reload_pending = false;
    table.cursor = 0;
    CloseIndexedTextTableArchive(table);
}

void SetIndexedTextTableSourceName(IndexedTextTableContext& table, const char* source_name) {
    table.source_name = source_name != nullptr ? source_name : "";
}

bool RefreshIndexedTextTableCursor(IndexedTextTableContext& table) {
    if (!table.source_loaded) {
        return false;
    }
    if (table.reload_pending) {
        table.cursor = 0;
        table.reload_pending = false;
    }
    return table.cursor < table.source_text.size();
}

const char* TokenizeIndexedTextTableSource(char* text, const char* delimiters) {
    return std::strtok(text, delimiters);
}

bool RewindIndexedTextTableSource(IndexedTextTableContext& table) {
    if (!table.source_loaded) {
        return false;
    }
    if (table.memory_source) {
        table.source_text = table.original_memory_source;
    }
    table.cursor = 0;
    table.reload_pending = true;
    return true;
}

void SkipIndexedTextTableLine(IndexedTextTableContext& table) {
    if (table.cursor >= table.source_text.size()) {
        return;
    }
    while (table.cursor < table.source_text.size() &&
        table.source_text[table.cursor] != '\n') {
        ++table.cursor;
    }
    if (table.cursor < table.source_text.size()) {
        ++table.cursor;
    }
}

void ConfigureIndexedTextTableDelimiters(IndexedTextTableContext& table,
    std::vector<std::string> delimiters) {
    table.delimiters = std::move(delimiters);
}

std::string_view NextIndexedTextTableToken(IndexedTextTableContext& table,
    u32* delimiter_index) {
    if (delimiter_index != nullptr) {
        *delimiter_index = 0xffffffffu;
    }
    if (!RefreshIndexedTextTableCursor(table)) {
        return {};
    }

    const std::size_t begin = table.cursor;
    while (table.cursor < table.source_text.size()) {
        for (std::size_t i = 0; i < table.delimiters.size(); ++i) {
            const std::string& delimiter = table.delimiters[i];
            if (!delimiter.empty() &&
                table.source_text.compare(table.cursor, delimiter.size(), delimiter) == 0) {
                if (delimiter_index != nullptr) {
                    *delimiter_index = static_cast<u32>(i);
                }
                const std::size_t end = table.cursor;
                table.cursor += delimiter.size();
                return std::string_view(table.source_text.data() + begin, end - begin);
            }
        }
        ++table.cursor;
    }
    return std::string_view(table.source_text.data() + begin, table.cursor - begin);
}

IndexedTextTableContext& StartupAuxiliaryIndexedTextTable(u32 slot) {
    const std::size_t index = slot < kStartupAuxiliaryTableCount ? slot : 0;
    return g_startup_auxiliary_tables[index];
}

void ConstructStartupIndexedTextTable0() {
    InitializeStartupIndexedTextTable0();
    RegisterStartupIndexedTextTable0Destructor();
}

void InitializeStartupIndexedTextTable0() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[0]);
}

void RegisterStartupIndexedTextTable0Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[0], DestroyStartupIndexedTextTable0);
}

void DestroyStartupIndexedTextTable0() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[0]);
}

void ConstructStartupIndexedTextTable1() {
    InitializeStartupIndexedTextTable1();
    RegisterStartupIndexedTextTable1Destructor();
}

void InitializeStartupIndexedTextTable1() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[1]);
}

void RegisterStartupIndexedTextTable1Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[1], DestroyStartupIndexedTextTable1);
}

void DestroyStartupIndexedTextTable1() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[1]);
}

void ConstructStartupIndexedTextTable2() {
    InitializeStartupIndexedTextTable2();
    RegisterStartupIndexedTextTable2Destructor();
}

void InitializeStartupIndexedTextTable2() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[2]);
}

void RegisterStartupIndexedTextTable2Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[2], DestroyStartupIndexedTextTable2);
}

void DestroyStartupIndexedTextTable2() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[2]);
}

void ConstructStartupIndexedTextTable3() {
    InitializeStartupIndexedTextTable3();
    RegisterStartupIndexedTextTable3Destructor();
}

void InitializeStartupIndexedTextTable3() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[3]);
}

void RegisterStartupIndexedTextTable3Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[3], DestroyStartupIndexedTextTable3);
}

void DestroyStartupIndexedTextTable3() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[3]);
}

void ConstructStartupIndexedTextTable4() {
    InitializeStartupIndexedTextTable4();
    RegisterStartupIndexedTextTable4Destructor();
}

void InitializeStartupIndexedTextTable4() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[4]);
}

void RegisterStartupIndexedTextTable4Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[4], DestroyStartupIndexedTextTable4);
}

void DestroyStartupIndexedTextTable4() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[4]);
}

void ConstructStartupIndexedTextTable5() {
    InitializeStartupIndexedTextTable5();
    RegisterStartupIndexedTextTable5Destructor();
}

void InitializeStartupIndexedTextTable5() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[5]);
}

void RegisterStartupIndexedTextTable5Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[5], DestroyStartupIndexedTextTable5);
}

void DestroyStartupIndexedTextTable5() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[5]);
}

void ConstructStartupIndexedTextTable6() {
    InitializeStartupIndexedTextTable6();
    RegisterStartupIndexedTextTable6Destructor();
}

void InitializeStartupIndexedTextTable6() {
    InitializeIndexedTextTableContext(g_startup_auxiliary_tables[6]);
}

void RegisterStartupIndexedTextTable6Destructor() {
    register_startup_destructor(g_startup_auxiliary_tables[6], DestroyStartupIndexedTextTable6);
}

void DestroyStartupIndexedTextTable6() {
    DestroyIndexedTextTableContext(g_startup_auxiliary_tables[6]);
}

}
