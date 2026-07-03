#pragma once

#include "ranker_types.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ranker {

struct TextTable {
    std::vector<char> storage;
    std::vector<std::string_view> rows;
};

struct StartupTextTables {
    TextTable platform_rows;
    TextTable message_rows;
    std::vector<TextTable> auxiliary_rows;
    bool loaded = false;
};

bool LoadStartupPlatformTextTables(const char* archive_name, u32 record_index);
bool LoadStartupMessageTextTable(const char* archive_name, u32 record_index);
bool LoadStartupJw217TextTables();
void ReleaseStartupPlatformTextTables();
void ReleaseStartupMessageTextTable();

const StartupTextTables& startup_text_tables();
std::size_t startup_platform_text_count();
std::size_t startup_message_text_count();

}
