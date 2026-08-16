#include "ranker_unit_death_resources.h"

#include <cctype>

namespace ranker {
namespace {

bool ascii_equal_yes(const u8* first, const u8* last) {
    return last - first == 3 &&
        std::tolower(static_cast<unsigned char>(first[0])) == 'y' &&
        std::tolower(static_cast<unsigned char>(first[1])) == 'e' &&
        std::tolower(static_cast<unsigned char>(first[2])) == 's';
}

}

std::array<bool, kUnitDefinitionResourceCount> ParseUnitDeathResourceManifest(
    const u8* bytes, std::size_t byte_count) {
    std::array<bool, kUnitDefinitionResourceCount> selected{};
    if (bytes == nullptr) {
        return selected;
    }

    const u8* cursor = bytes;
    const u8* const end = bytes + byte_count;
    while (cursor < end) {
        const u8* line_end = cursor;
        while (line_end < end && *line_end != '\r' && *line_end != '\n') {
            ++line_end;
        }

        const u8* token = cursor;
        while (token < line_end && std::isspace(static_cast<unsigned char>(*token))) {
            ++token;
        }
        u32 unit_type = 0;
        bool has_digits = false;
        while (token < line_end && *token >= '0' && *token <= '9') {
            has_digits = true;
            unit_type = unit_type * 10u + static_cast<u32>(*token - '0');
            ++token;
        }
        while (token < line_end && std::isspace(static_cast<unsigned char>(*token))) {
            ++token;
        }
        if (has_digits && unit_type < selected.size() && token < line_end &&
            *token == '=') {
            ++token;
            while (token < line_end && *token != '"') {
                ++token;
            }
            if (token < line_end) {
                const u8* const value_begin = ++token;
                while (token < line_end && *token != '"') {
                    ++token;
                }
                selected[unit_type] = ascii_equal_yes(value_begin, token);
            }
        }

        cursor = line_end;
        while (cursor < end && (*cursor == '\r' || *cursor == '\n')) {
            ++cursor;
        }
    }
    return selected;
}

}
