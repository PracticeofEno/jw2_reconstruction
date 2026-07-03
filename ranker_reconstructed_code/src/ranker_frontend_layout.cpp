#include "ranker_frontend_layout.h"

#include "ranker_trc.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ranker {
namespace {

bool equals_ascii_case_insensitive(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        const unsigned char lc = static_cast<unsigned char>(*left++);
        const unsigned char rc = static_cast<unsigned char>(*right++);
        if (std::tolower(lc) != std::tolower(rc)) {
            return false;
        }
    }
    return *left == '\0' && *right == '\0';
}

std::string trim_layout_line(std::string line) {
    const std::size_t comment = line.find_first_of("#;");
    if (comment != std::string::npos) {
        line.resize(comment);
    }

    const std::size_t first = line.find_first_not_of(" ,\t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = line.find_last_not_of(" ,\t\r\n");
    return line.substr(first, last - first + 1);
}

bool parse_layout_row(const std::string& line, FrontendLayoutRect& out_rect) {
    std::string normalized = line;
    for (char& ch : normalized) {
        if (ch == ',') {
            ch = ' ';
        }
    }

    std::istringstream stream(normalized);
    long values[4]{};
    for (long& value : values) {
        if (!(stream >> value)) {
            return false;
        }
        if (value < std::numeric_limits<i32>::min() ||
            value > std::numeric_limits<i32>::max()) {
            return false;
        }
    }

    out_rect.x = static_cast<i32>(values[0]);
    out_rect.y = static_cast<i32>(values[1]);
    out_rect.width = static_cast<i32>(values[2]);
    out_rect.height = static_cast<i32>(values[3]);
    return true;
}

std::vector<FrontendLayoutRect> parse_layout_rows(const char* text) {
    std::vector<FrontendLayoutRect> rows;
    if (text == nullptr || equals_ascii_case_insensitive(text, "NULL")) {
        return rows;
    }

    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim_layout_line(line);
        if (line.empty()) {
            continue;
        }
        FrontendLayoutRect rect{};
        if (parse_layout_row(line, rect)) {
            rows.push_back(rect);
        }
    }
    return rows;
}

bool assign_layout_rows(FrontendLayoutRectTable& table,
    const std::vector<FrontendLayoutRect>& rows) {
    ReleaseFrontendLayoutRectTable(table);
    if (rows.empty()) {
        return false;
    }
    if (rows.size() > std::numeric_limits<u32>::max()) {
        return false;
    }

    const std::size_t byte_count = rows.size() * sizeof(FrontendLayoutRect);
    auto* rects = static_cast<FrontendLayoutRect*>(std::malloc(byte_count));
    if (rects == nullptr) {
        return false;
    }
    std::memcpy(rects, rows.data(), byte_count);
    table.count = static_cast<u32>(rows.size());
    table.rects = rects;
    return true;
}

}

void ReleaseFrontendLayoutRectTable(FrontendLayoutRectTable& table) {
    if (table.rects != nullptr) {
        std::free(table.rects);
        table.rects = nullptr;
    }
    table.count = 0;
}

bool BuildFrontendLayoutRectTable(FrontendLayoutRectTable& table, const char* text) {
    const std::vector<FrontendLayoutRect> rows = parse_layout_rows(text);
    return assign_layout_rows(table, rows);
}

bool ParseFrontendLayoutText(FrontendLayoutRectTable& table, const char* text) {
    return BuildFrontendLayoutRectTable(table, text);
}

bool LoadFrontendLayoutFromText(FrontendLayoutRectTable& table, const char* path) {
    ReleaseFrontendLayoutRectTable(table);
    if (path == nullptr) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    text.push_back('\0');
    return BuildFrontendLayoutRectTable(table, text.c_str());
}

bool LoadFrontendLayoutFromTrcRecord(FrontendLayoutRectTable& table,
    const char* archive_name, u32 record_index) {
    ReleaseFrontendLayoutRectTable(table);
    std::vector<u8> record;
    if (archive_name == nullptr ||
        !LoadTrcRecordAlloc(archive_name, record_index, record, 1) ||
        record.empty()) {
        return false;
    }

    const char* text = reinterpret_cast<const char*>(record.data());
    if (equals_ascii_case_insensitive(text, "NULL")) {
        return false;
    }
    return ParseFrontendLayoutText(table, text);
}

bool LoadFrontendLayoutFromJw219TrcRecord(FrontendLayoutRectTable& table,
    u32 record_index) {
    return LoadFrontendLayoutFromTrcRecord(table, "Jw2_19.trc", record_index);
}

std::vector<FrontendLayoutRect> CopyFrontendLayoutRectTable(
    const FrontendLayoutRectTable& table) {
    if (table.rects == nullptr || table.count == 0) {
        return {};
    }
    return std::vector<FrontendLayoutRect>(table.rects,
        table.rects + table.count);
}

}
