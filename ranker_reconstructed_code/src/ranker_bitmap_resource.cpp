#include "ranker_bitmap_resource.h"

#include "ranker_crt_runtime.h"
#include "ranker_trc.h"

#include <cstring>

namespace ranker {
namespace {

constexpr std::size_t kBmpFileHeaderSize = 0x0e;
constexpr std::size_t kBmpInfoHeaderSize = 0x28;
constexpr u16 kBmpMagic = 0x4d42;

u16 read_le_u16(const u8* p) {
    return static_cast<u16>(p[0]) | static_cast<u16>(p[1] << 8);
}

u32 read_le_u32(const u8* p) {
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8) |
        (static_cast<u32>(p[2]) << 16) |
        (static_cast<u32>(p[3]) << 24);
}

i32 read_le_i32(const u8* p) {
    return WrappedU32ToI32(read_le_u32(p));
}

bool has_range(const std::vector<u8>& bytes, std::size_t offset, std::size_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

const u8* pointer_at(const BitmapMemoryResource& resource, u32 offset) {
    if (resource.owned_bytes.empty() ||
        static_cast<std::size_t>(offset) >= resource.owned_bytes.size()) {
        return nullptr;
    }
    return resource.owned_bytes.data() + offset;
}

#ifdef _WIN32
bool create_indexed_palette(BitmapMemoryResource& resource, const u8* info_header,
    u16 bit_count, u32 colors_used) {
    if (bit_count >= 9) {
        return true;
    }

    const u32 palette_entries = colors_used != 0 ? colors_used : (1u << bit_count);
    if (palette_entries == 0 || palette_entries > 256) {
        return false;
    }

    const std::size_t palette_offset = kBmpFileHeaderSize + kBmpInfoHeaderSize;
    const std::size_t palette_bytes = static_cast<std::size_t>(palette_entries) * 4u;
    if (!has_range(resource.owned_bytes, palette_offset, palette_bytes) ||
        palette_offset + palette_bytes > resource.pixel_offset) {
        return false;
    }

    const std::size_t log_palette_bytes =
        sizeof(LOGPALETTE) + (palette_entries - 1u) * sizeof(PALETTEENTRY);
    std::vector<u8> storage(log_palette_bytes, 0);
    auto* logical_palette = reinterpret_cast<LOGPALETTE*>(storage.data());
    logical_palette->palVersion = 0x0300;
    logical_palette->palNumEntries = static_cast<WORD>(palette_entries);

    const u8* source = resource.owned_bytes.data() + palette_offset;
    for (u32 i = 0; i < palette_entries; ++i) {
        PALETTEENTRY& entry = logical_palette->palPalEntry[i];
        entry.peRed = source[i * 4u + 2u];
        entry.peGreen = source[i * 4u + 1u];
        entry.peBlue = source[i * 4u];
        entry.peFlags = 0;
    }

    (void)info_header;
    resource.palette = CreatePalette(logical_palette);
    return resource.palette != nullptr;
}
#else
bool create_indexed_palette(BitmapMemoryResource&, const u8*, u16, u32) {
    return true;
}
#endif

bool parse_bitmap_resource(BitmapMemoryResource& resource) {
    if (!has_range(resource.owned_bytes, 0, kBmpFileHeaderSize + kBmpInfoHeaderSize)) {
        return false;
    }

    const u8* file_header = resource.owned_bytes.data();
    if (read_le_u16(file_header) != kBmpMagic) {
        return false;
    }

    const u32 pixel_offset = read_le_u32(file_header + 0x0a);
    if (!has_range(resource.owned_bytes, pixel_offset, 1)) {
        return false;
    }

    const u8* info_header = file_header + kBmpFileHeaderSize;

    const u16 bit_count = read_le_u16(info_header + 0x0e);
    const u32 colors_used = read_le_u32(info_header + 0x20);

    resource.file_header_offset = 0;
    resource.info_header_offset = static_cast<u32>(kBmpFileHeaderSize);
    resource.pixel_offset = pixel_offset;
    resource.source_x = 0;
    resource.source_y = 0;
    resource.width = read_le_i32(info_header + 0x04);
    resource.height = read_le_i32(info_header + 0x08);

    if (!create_indexed_palette(resource, info_header, bit_count, colors_used)) {
        return false;
    }

    resource.loaded = true;
    return true;
}

#ifdef _WIN32
bool has_owned_bitmap_bytes(const BitmapMemoryResource& resource) {
    return resource.data() != nullptr;
}

HPALETTE select_resource_palette(const BitmapMemoryResource& resource, HDC dc) {
    if (resource.palette == nullptr) {
        return nullptr;
    }
    return SelectPalette(dc, resource.palette, TRUE);
}

void restore_palette(HDC dc, HPALETTE old_palette) {
    if (old_palette != nullptr) {
        SelectPalette(dc, old_palette, TRUE);
    }
}
#endif

} // namespace

const u8* BitmapMemoryResource::data() const {
    return owned_bytes.empty() ? nullptr : owned_bytes.data();
}

const u8* BitmapMemoryResource::file_header() const {
    return pointer_at(*this, file_header_offset);
}

const u8* BitmapMemoryResource::bitmap_info() const {
    return pointer_at(*this, info_header_offset);
}

const u8* BitmapMemoryResource::pixels() const {
    return pointer_at(*this, pixel_offset);
}

BitmapMemoryResource& InitializeBitmapMemoryResource(BitmapMemoryResource& resource) {
    resource.loaded = false;
    resource.owned_bytes.clear();
    resource.palette = nullptr;
    return resource;
}

void HandleBitmapMemoryResourceDestructor(BitmapMemoryResource& resource) {
    ReleaseBitmapMemoryResource(resource);
}

void ReleaseBitmapMemoryResource(BitmapMemoryResource& resource) {
#ifdef _WIN32
    if (resource.palette != nullptr) {
        DeleteObject(resource.palette);
        resource.palette = nullptr;
    }
#endif
    std::vector<u8>{}.swap(resource.owned_bytes);
    resource.loaded = false;
    resource.palette = nullptr;
}

bool LoadBitmapMemoryResourceFromOwnedBytes(BitmapMemoryResource& resource,
    std::vector<u8> bytes) {
    ReleaseBitmapMemoryResource(resource);
    if (bytes.empty()) {
        return false;
    }

    BitmapMemoryResource next;
    next.owned_bytes = std::move(bytes);
    if (!parse_bitmap_resource(next)) {
        ReleaseBitmapMemoryResource(next);
        return false;
    }

    resource = std::move(next);
    return resource.loaded;
}

bool LoadBitmapMemoryResourceFromMemory(BitmapMemoryResource& resource, const void* data,
    std::size_t size) {
    if (data == nullptr && size != 0) {
        ReleaseBitmapMemoryResource(resource);
        return false;
    }

    std::vector<u8> bytes(size);
    if (size != 0) {
        std::memcpy(bytes.data(), data, size);
    }
    return LoadBitmapMemoryResourceFromOwnedBytes(resource, std::move(bytes));
}

bool LoadBitmapMemoryResourceFromFile(BitmapMemoryResource& resource, const char* path) {
    if (path == nullptr) {
        return false;
    }

    FILE* file = CrtFopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    const long length = CrtFileDescriptorLength(CrtStreamFileDescriptor(file));
    if (length < 10) {
        CrtFclose(file);
        return false;
    }

    void* allocation = _malloc(static_cast<std::size_t>(length));
    if (allocation == nullptr) {
        CrtFclose(file);
        return false;
    }

    if (CrtFread(allocation, static_cast<std::size_t>(length), 1, file) != 1) {
        CrtFree(allocation);
        CrtFclose(file);
        return false;
    }

    const bool loaded = LoadBitmapMemoryResourceFromMemory(
        resource, allocation, static_cast<std::size_t>(length));
    CrtFree(allocation);
    // Original 00509a20 omits CrtFclose on the successful read/load path.
    return loaded;
}

bool LoadBitmapMemoryResourceFromTrcRecord(BitmapMemoryResource& resource,
    const char* archive_name, u32 record_index) {
    std::vector<u8> bytes;
    if (!LoadTrcRecordAlloc(archive_name, record_index, bytes, 0)) {
        return false;
    }
    return LoadBitmapMemoryResourceFromOwnedBytes(resource, std::move(bytes));
}

i32 GetBitmapMemoryResourceWidth(const BitmapMemoryResource& resource) {
    return resource.width;
}

i32 GetBitmapMemoryResourceHeight(const BitmapMemoryResource& resource) {
    return resource.height;
}

#ifdef _WIN32
i32 StretchBitmapMemoryResourceToDc(const BitmapMemoryResource& resource, HDC dc,
    i32 x, i32 y) {
    if (!has_owned_bitmap_bytes(resource) || dc == nullptr) {
        return 0;
    }

    HPALETTE old_palette = select_resource_palette(resource, dc);
    SetStretchBltMode(dc, COLORONCOLOR);
    const i32 result = StretchDIBits(dc, x, y, resource.width, resource.height,
        resource.source_x, resource.source_y, resource.width, resource.height,
        resource.pixels(), reinterpret_cast<const BITMAPINFO*>(resource.bitmap_info()),
        DIB_RGB_COLORS, SRCCOPY);
    restore_palette(dc, old_palette);
    return result;
}

i32 DrawBitmapMemoryResourceToDcAtPoint(const BitmapMemoryResource& resource, HDC dc,
    const POINT* point) {
    if (!has_owned_bitmap_bytes(resource) || dc == nullptr || point == nullptr) {
        return 0;
    }

    HPALETTE old_palette = select_resource_palette(resource, dc);
    SetStretchBltMode(dc, COLORONCOLOR);
    const i32 result = SetDIBitsToDevice(dc, point->x, point->y,
        static_cast<DWORD>(resource.width), static_cast<DWORD>(resource.height), 0, 0, 0,
        static_cast<UINT>(resource.height), resource.pixels(),
        reinterpret_cast<const BITMAPINFO*>(resource.bitmap_info()), DIB_RGB_COLORS);
    restore_palette(dc, old_palette);
    return result;
}

i32 StretchBitmapMemoryResourceRectToDc(const BitmapMemoryResource& resource, HDC dc,
    const BitmapDrawRect& destination, const BitmapDrawRect& source) {
    if (!has_owned_bitmap_bytes(resource) || dc == nullptr) {
        return 0;
    }

    HPALETTE old_palette = select_resource_palette(resource, dc);
    SetStretchBltMode(dc, COLORONCOLOR);
    const i32 result = StretchDIBits(dc, destination.x, destination.y,
        destination.width, destination.height, source.x, source.y, source.width,
        source.height, resource.pixels(),
        reinterpret_cast<const BITMAPINFO*>(resource.bitmap_info()), DIB_RGB_COLORS,
        SRCCOPY);
    restore_palette(dc, old_palette);
    return result;
}
#endif

}
