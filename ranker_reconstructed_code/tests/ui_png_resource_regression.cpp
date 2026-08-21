#include "ranker_ui_png_resource.h"

#ifdef _WIN32

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    const bool executable_relative =
        argc == 5 && std::strcmp(argv[1], "--executable-relative") == 0;
    if (argc != 2 && argc != 4 && !executable_relative) {
        std::fprintf(stderr,
            "expected one extracted UI PNG path and optional dimensions, or "
            "--executable-relative path width height\n");
        return 1;
    }
    const char* png_path = executable_relative ? argv[2] : argv[1];
    const int dimension_offset = executable_relative ? 1 : 0;
    const bool dimensions_provided = argc == 4 || executable_relative;
    const int expected_width = dimensions_provided ?
        std::atoi(argv[2 + dimension_offset]) : 336;
    const int expected_height = dimensions_provided ?
        std::atoi(argv[3 + dimension_offset]) : 103;
    if (expected_width <= 0 || expected_height <= 0) {
        std::fprintf(stderr, "invalid expected UI PNG dimensions\n");
        return 1;
    }

    ranker::UiPngResource resource;
    ranker::InitializeUiPngResource(resource);
    const bool loaded = executable_relative ?
        ranker::LoadUiPngResourceFromExecutableRelativeFile(
            resource, png_path) :
        ranker::LoadUiPngResourceFromFile(resource, png_path);
    if (!loaded ||
        resource.width != expected_width || resource.height != expected_height) {
        std::fprintf(stderr, "failed to load extracted ARGB UI PNG\n");
        return 1;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = resource.width;
    info.bmiHeader.biHeight = -resource.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP target = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
        &pixels, nullptr, 0);
    if (dc == nullptr || target == nullptr || pixels == nullptr) {
        std::fprintf(stderr, "failed to create alpha composition target\n");
        ranker::ReleaseUiPngResource(resource);
        return 1;
    }

    HGDIOBJ old_bitmap = SelectObject(dc, target);
    auto* words = static_cast<unsigned long*>(pixels);
    const std::size_t pixel_count =
        static_cast<std::size_t>(resource.width) * resource.height;
    constexpr unsigned long kBackground = 0xff38220cul;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        words[i] = kBackground;
    }

    const bool drawn = ranker::DrawUiPngResourceToDc(resource, dc,
        ranker::UiPngRect{0, 0, resource.width, resource.height});
    const bool vertically_flipped =
        ranker::DrawUiPngResourceRectToDcVerticallyFlipped(resource, dc,
            ranker::UiPngRect{0, 0, resource.width, resource.height},
            ranker::UiPngRect{0, 0, resource.width, resource.height});
    std::size_t changed = 0;
    std::size_t unchanged = 0;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        if (words[i] == kBackground) {
            ++unchanged;
        } else {
            ++changed;
        }
    }

    SelectObject(dc, old_bitmap);
    DeleteObject(target);
    DeleteDC(dc);
    ranker::ReleaseUiPngResource(resource);
    if (!drawn || !vertically_flipped || changed == 0 || unchanged == 0) {
        std::fprintf(stderr, "ARGB sprite did not compose over the target\n");
        return 1;
    }
    return 0;
}

#else

int main() {
    return 0;
}

#endif
