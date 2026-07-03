#pragma once

#include "ranker_types.h"

#include <cstddef>
#include <vector>

namespace ranker {

struct FrontendLayoutRect {
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
};

struct FrontendLayoutRectTable {
    u32 count = 0;
    FrontendLayoutRect* rects = nullptr;
};

void ReleaseFrontendLayoutRectTable(FrontendLayoutRectTable& table);
bool BuildFrontendLayoutRectTable(FrontendLayoutRectTable& table, const char* text);
bool ParseFrontendLayoutText(FrontendLayoutRectTable& table, const char* text);
bool LoadFrontendLayoutFromText(FrontendLayoutRectTable& table, const char* path);
bool LoadFrontendLayoutFromTrcRecord(FrontendLayoutRectTable& table,
    const char* archive_name, u32 record_index);
bool LoadFrontendLayoutFromJw219TrcRecord(FrontendLayoutRectTable& table,
    u32 record_index);
std::vector<FrontendLayoutRect> CopyFrontendLayoutRectTable(
    const FrontendLayoutRectTable& table);

}
