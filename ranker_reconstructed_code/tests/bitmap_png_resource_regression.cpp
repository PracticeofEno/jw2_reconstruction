#include "ranker_bitmap_resource.h"
#include "ranker_crt_runtime.h"
#include "ranker_trc.h"

#include <cstdio>

namespace {

u16 read_le_u16(const u8* bytes) {
    return static_cast<u16>(bytes[0]) |
        static_cast<u16>(bytes[1] << 8);
}

bool pixel_matches(const ranker::BitmapMemoryResource& resource,
    u32 x, u32 y, u8 red, u8 green, u8 blue) {
    const std::size_t stride =
        (static_cast<std::size_t>(resource.width) * 3u + 3u) & ~std::size_t{3u};
    const u8* pixel = resource.pixels() +
        static_cast<std::size_t>(resource.height - 1 - static_cast<i32>(y)) * stride +
        static_cast<std::size_t>(x) * 3u;
    return pixel[0] == blue && pixel[1] == green && pixel[2] == red;
}

}

namespace ranker {

// The PNG regression does not exercise the legacy BMP/TRC file paths.  Keep
// their production dependencies out of this focused test target.
FILE* CrtFopen(const char*, const char*) {
    return nullptr;
}

int CrtFclose(FILE*) {
    return 0;
}

std::size_t CrtFread(void*, std::size_t, std::size_t, FILE*) {
    return 0;
}

int CrtStreamFileDescriptor(FILE*) {
    return -1;
}

long CrtFileDescriptorLength(int) {
    return -1;
}

void* _malloc(std::size_t) {
    return nullptr;
}

void CrtFree(void*) {
}

bool LoadTrcRecordAlloc(const char*, u32, std::vector<u8>&,
    std::size_t, TrcDirectoryEntry*) {
    return false;
}

}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "expected the lobby PNG path\n");
        return 1;
    }

    ranker::BitmapMemoryResource resource;
    ranker::InitializeBitmapMemoryResource(resource);
    if (!ranker::LoadPngBitmapMemoryResourceFromFile(resource, argv[1])) {
        std::fprintf(stderr, "failed to load lobby PNG\n");
        return 1;
    }

    const u8* info = resource.bitmap_info();
    const bool valid = resource.loaded && resource.file_header() != nullptr &&
        info != nullptr && resource.pixels() != nullptr &&
        ranker::GetBitmapMemoryResourceWidth(resource) == 1446 &&
        ranker::GetBitmapMemoryResourceHeight(resource) == 1087 &&
        read_le_u16(info + 0x0c) == 1 && read_le_u16(info + 0x0e) == 24 &&
        pixel_matches(resource, 0, 0, 47, 23, 13) &&
        pixel_matches(resource, 0, 1086, 16, 13, 8) &&
        pixel_matches(resource, 100, 100, 230, 195, 134);
    ranker::ReleaseBitmapMemoryResource(resource);
    if (!valid) {
        std::fprintf(stderr, "decoded lobby PNG has unexpected bitmap metadata\n");
        return 1;
    }
    return 0;
}
