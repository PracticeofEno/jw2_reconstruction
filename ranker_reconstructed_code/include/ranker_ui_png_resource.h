#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

struct UiPngRect {
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
};

struct UiPngResource {
    bool loaded = false;
    void* native_bitmap = nullptr;
    i32 width = 0;
    i32 height = 0;
};

UiPngResource& InitializeUiPngResource(UiPngResource& resource);
void ReleaseUiPngResource(UiPngResource& resource);
bool LoadUiPngResourceFromFile(UiPngResource& resource, const char* path);

#ifdef _WIN32
bool LoadUiPngResourceFromExecutableRelativeFile(
    UiPngResource& resource, const char* relative_path);
bool DrawUiPngResourceRectToDc(const UiPngResource& resource, HDC dc,
    const UiPngRect& destination, const UiPngRect& source);
bool DrawUiPngResourceRectToDcVerticallyFlipped(
    const UiPngResource& resource, HDC dc,
    const UiPngRect& destination, const UiPngRect& source);
bool DrawUiPngResourceToDc(const UiPngResource& resource, HDC dc,
    const UiPngRect& destination);
#endif

}
