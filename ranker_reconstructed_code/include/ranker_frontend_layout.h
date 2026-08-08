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

struct FrontendLayoutPoint {
    i32 x = 0;
    i32 y = 0;
};

constexpr FrontendLayoutPoint FrontendLayoutOrigin(
    const FrontendLayoutRect& bounds) {
    return {bounds.x, bounds.y};
}

constexpr FrontendLayoutPoint CenteredFrontendLayoutOrigin(
    const FrontendLayoutRect& bounds, i32 width, i32 height) {
    return {
        bounds.x + (bounds.width - width) / 2,
        bounds.y + (bounds.height - height) / 2};
}

constexpr FrontendLayoutPoint CenteredContainedFrontendLayoutOrigin(
    const FrontendLayoutRect& bounds, i32 width, i32 height) {
    const i32 centered_x = (bounds.width - width) / 2;
    const i32 centered_y = (bounds.height - height) / 2;
    return {
        bounds.x + (centered_x > 0 ? centered_x : 0),
        bounds.y + (centered_y > 0 ? centered_y : 0)};
}

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
