#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstddef>
#include <vector>

namespace ranker {

struct BitmapDrawRect {
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
};

struct BitmapMemoryResource {
    bool loaded = false;
    std::vector<u8> owned_bytes;
#ifdef _WIN32
    HPALETTE palette = nullptr;
#else
    void* palette = nullptr;
#endif
    u32 file_header_offset = 0;
    u32 info_header_offset = 0;
    u32 pixel_offset = 0;
    i32 source_x = 0;
    i32 source_y = 0;
    i32 width = 0;
    i32 height = 0;

    const u8* data() const;
    const u8* file_header() const;
    const u8* bitmap_info() const;
    const u8* pixels() const;
};

BitmapMemoryResource& InitializeBitmapMemoryResource(BitmapMemoryResource& resource);
void HandleBitmapMemoryResourceDestructor(BitmapMemoryResource& resource);
void ReleaseBitmapMemoryResource(BitmapMemoryResource& resource);

bool LoadBitmapMemoryResourceFromOwnedBytes(BitmapMemoryResource& resource,
    std::vector<u8> bytes);
bool LoadBitmapMemoryResourceFromMemory(BitmapMemoryResource& resource, const void* data,
    std::size_t size);
bool LoadBitmapMemoryResourceFromFile(BitmapMemoryResource& resource, const char* path);
#ifdef _WIN32
bool LoadPngBitmapMemoryResourceFromFile(
    BitmapMemoryResource& resource, const char* path);
bool LoadBitmapMemoryResourceFromExecutableRelativeFile(
    BitmapMemoryResource& resource, const char* relative_path);
bool LoadPngBitmapMemoryResourceFromExecutableRelativeFile(
    BitmapMemoryResource& resource, const char* relative_path);
#endif
bool LoadBitmapMemoryResourceFromTrcRecord(BitmapMemoryResource& resource,
    const char* archive_name, u32 record_index);

i32 GetBitmapMemoryResourceWidth(const BitmapMemoryResource& resource);
i32 GetBitmapMemoryResourceHeight(const BitmapMemoryResource& resource);

#ifdef _WIN32
i32 StretchBitmapMemoryResourceToDc(const BitmapMemoryResource& resource, HDC dc,
    i32 x, i32 y);
i32 StretchBitmapMemoryResourceToClient(const BitmapMemoryResource& resource,
    HDC dc, HWND window);
i32 DrawBitmapMemoryResourceToDcAtPoint(const BitmapMemoryResource& resource, HDC dc,
    const POINT* point);
i32 StretchBitmapMemoryResourceRectToDc(const BitmapMemoryResource& resource, HDC dc,
    const BitmapDrawRect& destination, const BitmapDrawRect& source);
#endif

}
