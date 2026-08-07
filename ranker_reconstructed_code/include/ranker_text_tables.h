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
const char* startup_platform_row(std::size_t index, const char* fallback);
const char* startup_message_row(std::size_t index, const char* fallback);
const char* startup_production_resource_failure_row(u32 code);
std::string startup_platform_label_value(
    std::size_t index, const char* fallback, u32 value);
std::string startup_platform_ratio_value(
    std::size_t index, const char* fallback, u32 value, u32 total);
std::string startup_auxiliary_text_or_empty(u32 slot, u32 row);
std::string startup_action_name_or_empty(u32 action_id);
std::size_t startup_platform_text_count();
std::size_t startup_message_text_count();

}
