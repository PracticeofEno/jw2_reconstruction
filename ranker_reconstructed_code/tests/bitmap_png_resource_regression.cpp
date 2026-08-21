#include "ranker_bitmap_resource.h"
#include "ranker_crt_runtime.h"
#include "ranker_trc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    const bool executable_relative =
        argc == 5 && std::strcmp(argv[1], "--executable-relative") == 0;
    if (argc != 2 && argc != 4 && !executable_relative) {
        std::fprintf(stderr,
            "expected a PNG path and optional expected width/height, or "
            "--executable-relative path width height\n");
        return 1;
    }

    const char* png_path = executable_relative ? argv[2] : argv[1];
    const i32 dimension_offset = executable_relative ? 1 : 0;
    const bool dimensions_provided = argc == 4 || executable_relative;
    const i32 expected_width = dimensions_provided
        ? std::atoi(argv[2 + dimension_offset]) : 1446;
    const i32 expected_height = dimensions_provided
        ? std::atoi(argv[3 + dimension_offset]) : 1087;
    if (expected_width <= 0 || expected_height <= 0) {
        std::fprintf(stderr, "invalid expected PNG dimensions\n");
        return 1;
    }

    ranker::BitmapMemoryResource resource;
    ranker::InitializeBitmapMemoryResource(resource);
    const bool loaded = executable_relative
        ? ranker::LoadPngBitmapMemoryResourceFromExecutableRelativeFile(
            resource, png_path)
        : ranker::LoadPngBitmapMemoryResourceFromFile(resource, png_path);
    if (!loaded) {
        std::fprintf(stderr, "failed to load lobby PNG\n");
        return 1;
    }

    const u8* info = resource.bitmap_info();
    const bool valid = resource.loaded && resource.file_header() != nullptr &&
        info != nullptr && resource.pixels() != nullptr &&
        ranker::GetBitmapMemoryResourceWidth(resource) == expected_width &&
        ranker::GetBitmapMemoryResourceHeight(resource) == expected_height &&
        read_le_u16(info + 0x0c) == 1 && read_le_u16(info + 0x0e) == 24;
    const bool valid_lobby_signature = dimensions_provided ||
        (pixel_matches(resource, 0, 0, 47, 23, 13) &&
            pixel_matches(resource, 0, 1086, 16, 13, 8) &&
            pixel_matches(resource, 100, 100, 230, 195, 134));
    ranker::ReleaseBitmapMemoryResource(resource);
    if (!valid || !valid_lobby_signature) {
        std::fprintf(stderr, "decoded PNG has unexpected bitmap metadata\n");
        return 1;
    }
    return 0;
}
