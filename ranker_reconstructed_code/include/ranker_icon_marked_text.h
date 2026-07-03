#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

constexpr i32 kInlineIconMarkerWidth = 0x1c;

int FindInlineIconMarker(const char* text);

#ifdef _WIN32
int MeasureIconMarkedTextWidth(HDC dc, const char* text);
#endif

}
