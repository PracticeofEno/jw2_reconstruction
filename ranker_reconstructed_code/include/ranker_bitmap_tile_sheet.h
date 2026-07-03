#pragma once

#include "ranker_bitmap_resource.h"

namespace ranker {

constexpr i32 kBitmapTileSheetCellWidth = 0x1c;
constexpr i32 kBitmapTileSheetCellHeight = 0x0e;
constexpr u32 kBitmapTileSheetTrcRecord = 0x148;

struct BitmapTileSheetSelector {
    BitmapMemoryResource sheet;
    i32 selected_y = 0;
    i32 selected_x = 0;
};

void InitializeBitmapTileSheetSelector(BitmapTileSheetSelector& selector);
void DeleteBitmapTileSheetSelector(BitmapTileSheetSelector& selector);
void HandleBitmapTileSheetSelectorDestructor(BitmapTileSheetSelector& selector);
bool LoadBitmapTileSheetSelectorResource(BitmapTileSheetSelector& selector);
bool SetBitmapTileSheetSelection(BitmapTileSheetSelector& selector, i32 x, i32 y);
int HandleBitmapTileSheetSelectionChanged(BitmapTileSheetSelector& selector,
    i32 x, i32 y);
void ReleaseBitmapTileSheetResource(BitmapTileSheetSelector& selector);
void DrawSelectedBitmapTileSheetCell(const BitmapTileSheetSelector& selector, HDC dc);
void DrawBitmapTileSheetCellByIndex(const BitmapTileSheetSelector& selector, HDC dc,
    i32 x, i32 y, i32 cell_index);
int NotifyBitmapTileSheetSelectionIfValid(BitmapTileSheetSelector& selector);

#ifdef _WIN32
HBITMAP CreateBitmapTileSheetCellBitmapByIndex(const BitmapTileSheetSelector& selector,
    i32 cell_index);
HBITMAP CreateSelectedBitmapTileSheetCellBitmap(const BitmapTileSheetSelector& selector);
#endif

}
