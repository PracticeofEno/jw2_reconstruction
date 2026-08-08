#pragma once

#include "ranker_types.h"

#include <cstddef>
#include <vector>

namespace ranker {

constexpr i32 kLegacyFrontendLayoutWidth = 800;
constexpr i32 kLegacyFrontendLayoutHeight = 600;

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

constexpr i32 ScaleFrontendLayoutValue(i32 value, i32 source_extent,
    i32 target_extent) {
    if (source_extent <= 0 || target_extent <= 0 ||
        source_extent == target_extent) {
        return value;
    }
    const i64 product = static_cast<i64>(value) * target_extent;
    const i64 rounding = source_extent / 2;
    return static_cast<i32>(product >= 0
        ? (product + rounding) / source_extent
        : (product - rounding) / source_extent);
}

constexpr FrontendLayoutRect ScaleFrontendLayoutRect(
    const FrontendLayoutRect& rect, i32 target_width, i32 target_height) {
    return {
        ScaleFrontendLayoutValue(rect.x, kLegacyFrontendLayoutWidth, target_width),
        ScaleFrontendLayoutValue(rect.y, kLegacyFrontendLayoutHeight, target_height),
        ScaleFrontendLayoutValue(
            rect.width, kLegacyFrontendLayoutWidth, target_width),
        ScaleFrontendLayoutValue(
            rect.height, kLegacyFrontendLayoutHeight, target_height)};
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
void SetFrontendLayoutTargetSize(i32 width, i32 height);
FrontendLayoutPoint FrontendLayoutTargetSize();
void ScaleFrontendLayoutRectTable(FrontendLayoutRectTable& table,
    i32 target_width, i32 target_height);
std::vector<FrontendLayoutRect> CopyFrontendLayoutRectTable(
    const FrontendLayoutRectTable& table);

}
