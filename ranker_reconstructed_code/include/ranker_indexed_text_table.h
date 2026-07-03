#pragma once

#include "ranker_dpg_archive.h"
#include "ranker_types.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace ranker {

struct IndexedTextTableContext {
    DpgArchiveContext archive;
    std::string source_name;
    std::string source_text;
    std::string original_memory_source;
    std::vector<std::string> rows;
    std::vector<std::string> delimiters;
    std::size_t cursor = 0;
    bool source_loaded = false;
    bool memory_source = false;
    bool reload_pending = false;
    bool destructor_registered = false;
    std::string empty_row;
};

void InitializeIndexedTextTableContext(IndexedTextTableContext& table);
void DestroyIndexedTextTableContext(IndexedTextTableContext& table);
void ResetIndexedTextTableContext(IndexedTextTableContext& table);
bool BuildIndexedTextTableRows(IndexedTextTableContext& table);
const char* FindLastIndexedTextTableCharacter(const char* text, char value);
bool LoadIndexedTextTableFromFile(IndexedTextTableContext& table, const char* path);
bool LoadIndexedTextTableFromMemory(IndexedTextTableContext& table, const char* text);
std::string_view GetIndexedTextTableRow(const IndexedTextTableContext& table, u32 index);
void SetIndexedTextTableRow(IndexedTextTableContext& table, u32 index, const char* text);
void ReleaseIndexedTextTableRow(IndexedTextTableContext& table, u32 index);
void ReleaseIndexedTextTableRows(IndexedTextTableContext& table);
void InitializeIndexedTextTableReader(IndexedTextTableContext& table);
void DestroyIndexedTextTableReader(IndexedTextTableContext& table);
bool OpenIndexedTextTableArchive(IndexedTextTableContext& table, const char* archive_path);
void CloseIndexedTextTableArchive(IndexedTextTableContext& table);
bool LoadIndexedTextTableFileSource(IndexedTextTableContext& table, const char* path);
bool LoadIndexedTextTableMemorySource(IndexedTextTableContext& table, const char* text);
void ReleaseIndexedTextTableSource(IndexedTextTableContext& table);
void SetIndexedTextTableSourceName(IndexedTextTableContext& table, const char* source_name);
bool RefreshIndexedTextTableCursor(IndexedTextTableContext& table);
const char* TokenizeIndexedTextTableSource(char* text, const char* delimiters);
bool RewindIndexedTextTableSource(IndexedTextTableContext& table);
void SkipIndexedTextTableLine(IndexedTextTableContext& table);
void ConfigureIndexedTextTableDelimiters(IndexedTextTableContext& table,
    std::vector<std::string> delimiters);
std::string_view NextIndexedTextTableToken(IndexedTextTableContext& table,
    u32* delimiter_index);

IndexedTextTableContext& StartupAuxiliaryIndexedTextTable(u32 slot);
void ConstructStartupIndexedTextTable0();
void InitializeStartupIndexedTextTable0();
void RegisterStartupIndexedTextTable0Destructor();
void DestroyStartupIndexedTextTable0();
void ConstructStartupIndexedTextTable1();
void InitializeStartupIndexedTextTable1();
void RegisterStartupIndexedTextTable1Destructor();
void DestroyStartupIndexedTextTable1();
void ConstructStartupIndexedTextTable2();
void InitializeStartupIndexedTextTable2();
void RegisterStartupIndexedTextTable2Destructor();
void DestroyStartupIndexedTextTable2();
void ConstructStartupIndexedTextTable3();
void InitializeStartupIndexedTextTable3();
void RegisterStartupIndexedTextTable3Destructor();
void DestroyStartupIndexedTextTable3();
void ConstructStartupIndexedTextTable4();
void InitializeStartupIndexedTextTable4();
void RegisterStartupIndexedTextTable4Destructor();
void DestroyStartupIndexedTextTable4();
void ConstructStartupIndexedTextTable5();
void InitializeStartupIndexedTextTable5();
void RegisterStartupIndexedTextTable5Destructor();
void DestroyStartupIndexedTextTable5();
void ConstructStartupIndexedTextTable6();
void InitializeStartupIndexedTextTable6();
void RegisterStartupIndexedTextTable6Destructor();
void DestroyStartupIndexedTextTable6();

}
