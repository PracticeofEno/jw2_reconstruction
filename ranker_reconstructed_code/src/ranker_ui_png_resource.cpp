#include "ranker_ui_png_resource.h"

#ifdef _WIN32

#include <wtypes.h>
#include <gdiplus.h>

#include <limits>
#include <memory>
#include <string>

namespace ranker {
namespace {

bool ensure_ui_png_gdiplus() {
    static const ULONG_PTR token = [] {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR next_token = 0;
        return Gdiplus::GdiplusStartup(&next_token, &input, nullptr) == Gdiplus::Ok ?
            next_token : 0;
    }();
    return token != 0;
}

std::wstring widen_ui_png_path(const char* path) {
    if (path == nullptr) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring wide_path(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, path, -1,
            wide_path.data(), required) <= 0) {
        return {};
    }
    wide_path.resize(static_cast<std::size_t>(required - 1));
    return wide_path;
}

Gdiplus::Bitmap* native_bitmap(const UiPngResource& resource) {
    return static_cast<Gdiplus::Bitmap*>(resource.native_bitmap);
}

bool valid_rect(const UiPngRect& rect) {
    return rect.width > 0 && rect.height > 0;
}

} // namespace

UiPngResource& InitializeUiPngResource(UiPngResource& resource) {
    resource.loaded = false;
    resource.native_bitmap = nullptr;
    resource.width = 0;
    resource.height = 0;
    return resource;
}

void ReleaseUiPngResource(UiPngResource& resource) {
    delete native_bitmap(resource);
    resource.loaded = false;
    resource.native_bitmap = nullptr;
    resource.width = 0;
    resource.height = 0;
}

bool LoadUiPngResourceFromFile(UiPngResource& resource, const char* path) {
    if (path == nullptr || !ensure_ui_png_gdiplus()) {
        return false;
    }

    const std::wstring wide_path = widen_ui_png_path(path);
    if (wide_path.empty()) {
        return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> decoded(
        new Gdiplus::Bitmap(wide_path.c_str()));
    GUID raw_format{};
    if (decoded->GetLastStatus() != Gdiplus::Ok ||
        decoded->GetRawFormat(&raw_format) != Gdiplus::Ok ||
        !IsEqualGUID(raw_format, Gdiplus::ImageFormatPNG)) {
        return false;
    }

    const UINT width = decoded->GetWidth();
    const UINT height = decoded->GetHeight();
    if (width == 0 || height == 0 ||
        width > static_cast<UINT>(std::numeric_limits<i32>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<i32>::max())) {
        return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> alpha_bitmap(decoded->Clone(
        0, 0, static_cast<INT>(width), static_cast<INT>(height),
        PixelFormat32bppPARGB));
    if (alpha_bitmap == nullptr || alpha_bitmap->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    ReleaseUiPngResource(resource);
    resource.native_bitmap = alpha_bitmap.release();
    resource.width = static_cast<i32>(width);
    resource.height = static_cast<i32>(height);
    resource.loaded = true;
    return true;
}

bool LoadUiPngResourceFromExecutableRelativeFile(
    UiPngResource& resource, const char* relative_path) {
    if (relative_path == nullptr || relative_path[0] == '\0') {
        return false;
    }

    char module_path[MAX_PATH]{};
    const DWORD module_path_length = GetModuleFileNameA(
        nullptr, module_path, static_cast<DWORD>(sizeof(module_path)));
    if (module_path_length == 0 || module_path_length >= sizeof(module_path)) {
        return false;
    }

    std::string path(module_path, module_path_length);
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return false;
    }
    path.resize(separator + 1);
    path.append(relative_path);
    return LoadUiPngResourceFromFile(resource, path.c_str());
}

bool DrawUiPngResourceRectToDc(const UiPngResource& resource, HDC dc,
    const UiPngRect& destination, const UiPngRect& source) {
    Gdiplus::Bitmap* bitmap = native_bitmap(resource);
    if (!resource.loaded || bitmap == nullptr || dc == nullptr ||
        !valid_rect(destination) || !valid_rect(source) ||
        source.x < 0 || source.y < 0 ||
        source.width > resource.width - source.x ||
        source.height > resource.height - source.y) {
        return false;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Gdiplus::Rect destination_rect(destination.x, destination.y,
        destination.width, destination.height);
    return graphics.DrawImage(bitmap, destination_rect,
        source.x, source.y, source.width, source.height,
        Gdiplus::UnitPixel) == Gdiplus::Ok;
}

bool DrawUiPngResourceRectToDcVerticallyFlipped(
    const UiPngResource& resource, HDC dc,
    const UiPngRect& destination, const UiPngRect& source) {
    Gdiplus::Bitmap* bitmap = native_bitmap(resource);
    if (!resource.loaded || bitmap == nullptr || dc == nullptr ||
        !valid_rect(destination) || !valid_rect(source) ||
        source.x < 0 || source.y < 0 ||
        source.width > resource.width - source.x ||
        source.height > resource.height - source.y) {
        return false;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Gdiplus::Point destination_points[3] = {
        {destination.x, destination.y + destination.height},
        {destination.x + destination.width,
            destination.y + destination.height},
        {destination.x, destination.y},
    };
    return graphics.DrawImage(bitmap, destination_points, 3,
        source.x, source.y, source.width, source.height,
        Gdiplus::UnitPixel) == Gdiplus::Ok;
}

bool DrawUiPngResourceToDc(const UiPngResource& resource, HDC dc,
    const UiPngRect& destination) {
    const UiPngRect source{0, 0, resource.width, resource.height};
    return DrawUiPngResourceRectToDc(resource, dc, destination, source);
}

}

#else

namespace ranker {

UiPngResource& InitializeUiPngResource(UiPngResource& resource) {
    resource = {};
    return resource;
}

void ReleaseUiPngResource(UiPngResource& resource) {
    resource = {};
}

bool LoadUiPngResourceFromFile(UiPngResource&, const char*) {
    return false;
}

}

#endif
