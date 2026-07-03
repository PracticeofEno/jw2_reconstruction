#include "ranker_bitmap_tile_sheet.h"

namespace ranker {
namespace {

i32 snap_down(i32 value, i32 step) {
    return value - value % step;
}

bool selection_valid(const BitmapTileSheetSelector& selector) {
    return selector.selected_x >= 0 && selector.selected_y >= 0 &&
        selector.selected_x < GetBitmapMemoryResourceWidth(selector.sheet) &&
        selector.selected_y < GetBitmapMemoryResourceHeight(selector.sheet);
}

BitmapDrawRect source_rect_for(const BitmapMemoryResource& sheet, i32 source_x,
    i32 source_y) {
    return BitmapDrawRect{source_x,
        (GetBitmapMemoryResourceHeight(sheet) - kBitmapTileSheetCellHeight) - source_y,
        kBitmapTileSheetCellWidth, kBitmapTileSheetCellHeight};
}

void draw_selected_cell_unchecked(const BitmapTileSheetSelector& selector, HDC dc) {
    const BitmapDrawRect destination{1, 1, kBitmapTileSheetCellWidth,
        kBitmapTileSheetCellHeight};
    const BitmapDrawRect source =
        source_rect_for(selector.sheet, selector.selected_x, selector.selected_y);
    StretchBitmapMemoryResourceRectToDc(selector.sheet, dc, destination, source);
}

} // namespace

void InitializeBitmapTileSheetSelector(BitmapTileSheetSelector& selector) {
    InitializeBitmapMemoryResource(selector.sheet);
}

void DeleteBitmapTileSheetSelector(BitmapTileSheetSelector& selector) {
    HandleBitmapTileSheetSelectorDestructor(selector);
}

void HandleBitmapTileSheetSelectorDestructor(BitmapTileSheetSelector& selector) {
    ReleaseBitmapMemoryResource(selector.sheet);
}

bool LoadBitmapTileSheetSelectorResource(BitmapTileSheetSelector& selector) {
    LoadBitmapMemoryResourceFromTrcRecord(selector.sheet, "Jw2_19.trc",
        kBitmapTileSheetTrcRecord);
    return true;
}

bool SetBitmapTileSheetSelection(BitmapTileSheetSelector& selector, i32 x, i32 y) {
    const i32 snapped_y = snap_down(y, kBitmapTileSheetCellHeight);
    const i32 snapped_x = snap_down(x, kBitmapTileSheetCellWidth);
    if (snapped_y == selector.selected_y && snapped_x == selector.selected_x) {
        return false;
    }

    selector.selected_y = snapped_y;
    selector.selected_x = snapped_x;
    return selection_valid(selector);
}

int HandleBitmapTileSheetSelectionChanged(BitmapTileSheetSelector& selector,
    i32 x, i32 y) {
    const i32 row = y / kBitmapTileSheetCellHeight;
    const i32 row_offset =
        (GetBitmapMemoryResourceWidth(selector.sheet) * row) /
        kBitmapTileSheetCellWidth;
    return row_offset +
        (x / kBitmapTileSheetCellWidth);
}

void ReleaseBitmapTileSheetResource(BitmapTileSheetSelector& selector) {
    ReleaseBitmapMemoryResource(selector.sheet);
}

void DrawSelectedBitmapTileSheetCell(const BitmapTileSheetSelector& selector, HDC dc) {
    if (!selection_valid(selector)) {
        return;
    }

    draw_selected_cell_unchecked(selector, dc);
}

void DrawBitmapTileSheetCellByIndex(const BitmapTileSheetSelector& selector, HDC dc,
    i32 x, i32 y, i32 cell_index) {
    const i32 columns = GetBitmapMemoryResourceWidth(selector.sheet) /
        kBitmapTileSheetCellWidth;
    const i32 source_x = (cell_index % columns) *
        kBitmapTileSheetCellWidth;
    const i32 source_y = (cell_index / columns) *
        kBitmapTileSheetCellHeight;
    const BitmapDrawRect destination{x, y, kBitmapTileSheetCellWidth,
        kBitmapTileSheetCellHeight};
    const BitmapDrawRect source = source_rect_for(selector.sheet, source_x, source_y);
    StretchBitmapMemoryResourceRectToDc(selector.sheet, dc, destination, source);
}

int NotifyBitmapTileSheetSelectionIfValid(BitmapTileSheetSelector& selector) {
    if (!selection_valid(selector)) {
        return -1;
    }

    return HandleBitmapTileSheetSelectionChanged(selector, selector.selected_x,
        selector.selected_y);
}

#ifdef _WIN32
HBITMAP CreateBitmapTileSheetCellBitmapByIndex(const BitmapTileSheetSelector& selector,
    i32 cell_index) {
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, kBitmapTileSheetCellWidth,
        kBitmapTileSheetCellHeight);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    DrawBitmapTileSheetCellByIndex(selector, memory_dc, 0, 0, cell_index);
    SelectObject(memory_dc, old_bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    return bitmap;
}

HBITMAP CreateSelectedBitmapTileSheetCellBitmap(const BitmapTileSheetSelector& selector) {
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, kBitmapTileSheetCellWidth,
        kBitmapTileSheetCellHeight);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    draw_selected_cell_unchecked(selector, memory_dc);
    SelectObject(memory_dc, old_bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    return bitmap;
}
#endif

}
