#include "ranker_ui_png_resource.h"

#ifdef _WIN32

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "expected one extracted UI PNG path\n");
        return 1;
    }

    ranker::UiPngResource resource;
    ranker::InitializeUiPngResource(resource);
    if (!ranker::LoadUiPngResourceFromFile(resource, argv[1]) ||
        resource.width != 336 || resource.height != 103) {
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
    if (!drawn || changed == 0 || unchanged == 0) {
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
