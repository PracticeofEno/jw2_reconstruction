#include "ranker_screenshot.h"

#ifdef _WIN32
#include "ranker_crt_runtime.h"
#include "ranker_directx.h"
#include "ranker_palette_cache.h"
#include "ranker_trc.h"

#include <gdiplus.h>

#include <algorithm>
#include <cstring>

namespace ranker {
namespace {

ScreenshotState g_screenshot_state;
ULONG_PTR g_gdiplus_token = 0;

constexpr u16 kBlueMask = 0x001f;
constexpr u16 kGreen565Mask = 0x07e0;
constexpr u16 kRed565Mask = 0xf800;
constexpr u16 kGreen555Mask = 0x03e0;
constexpr u16 kRed555Mask = 0x7c00;
constexpr std::size_t kLegacyScreenshotPathBytes = 0x100;

bool ensure_gdiplus() {
    if (g_gdiplus_token != 0) {
        return true;
    }

    Gdiplus::GdiplusStartupInput input;
    const Gdiplus::Status status = Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr);
    g_screenshot_state.last_result = status == Gdiplus::Ok ? S_OK : E_FAIL;
    return status == Gdiplus::Ok;
}

bool get_jpeg_encoder_clsid(CLSID& clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || count == 0 ||
        bytes == 0) {
        g_screenshot_state.last_result = E_FAIL;
        return false;
    }

    std::vector<u8> storage(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) {
        g_screenshot_state.last_result = E_FAIL;
        return false;
    }

    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(encoders[i].MimeType, L"image/jpeg") == 0) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }

    g_screenshot_state.last_result = E_FAIL;
    return false;
}

std::wstring widen_path(const char* path) {
    if (path == nullptr) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring out(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path, -1, out.data(), required);
    return out;
}

std::string next_available_screenshot_path() {
    char path[kLegacyScreenshotPathBytes];
    HANDLE find = nullptr;
    WIN32_FIND_DATAA find_data;
    std::memset(path, 0xcc, sizeof(path));
    std::memset(&find_data, 0xcc, sizeof(find_data));
    while (find != INVALID_HANDLE_VALUE) {
        CrtSprintf(path, "%08d.jpg",
            static_cast<int>(g_screenshot_state.next_index));
        find = FindFirstFileA(path, &find_data);
        FindClose(find);
        ++g_screenshot_state.next_index;
    }
    return path;
}

bool copy_bitmap_to_bgr_state(Gdiplus::Bitmap& bitmap) {
    const UINT width = bitmap.GetWidth();
    const UINT height = bitmap.GetHeight();
    if (width == 0 || height == 0) {
        g_screenshot_state.last_result = E_FAIL;
        return false;
    }

    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData data{};
    Gdiplus::Status status = bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead,
        PixelFormat24bppRGB, &data);
    if (status != Gdiplus::Ok) {
        g_screenshot_state.last_result = E_FAIL;
        return false;
    }

    g_screenshot_state.image_width = width;
    g_screenshot_state.image_height = height;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3;
    const std::size_t stride_bytes = (row_bytes + 3u) & ~std::size_t{3u};
    g_screenshot_state.image_stride = static_cast<u32>(stride_bytes);
    g_screenshot_state.bgr_pixels.assign(stride_bytes * height, 0);

    const auto* base = static_cast<const u8*>(data.Scan0);
    const LONG stride = data.Stride;
    for (UINT y = 0; y < height; ++y) {
        const u8* src = stride >= 0 ?
            base + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) :
            base + static_cast<std::size_t>(height - 1 - y) *
                static_cast<std::size_t>(-stride);
        std::memcpy(g_screenshot_state.bgr_pixels.data() +
                static_cast<std::size_t>(y) * stride_bytes,
            src, row_bytes);
    }

    bitmap.UnlockBits(&data);
    g_screenshot_state.last_result = S_OK;
    return true;
}

bool decode_jpeg_memory_to_bgr_state(const void* bytes, std::size_t byte_count) {
    if (bytes == nullptr || byte_count == 0 || !ensure_gdiplus()) {
        g_screenshot_state.last_result = E_INVALIDARG;
        return false;
    }

    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (global == nullptr) {
        g_screenshot_state.last_result = E_OUTOFMEMORY;
        return false;
    }

    void* target = GlobalLock(global);
    if (target == nullptr) {
        GlobalFree(global);
        g_screenshot_state.last_result = E_OUTOFMEMORY;
        return false;
    }
    std::memcpy(target, bytes, byte_count);
    GlobalUnlock(global);

    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(global, TRUE, &stream);
    if (FAILED(hr)) {
        GlobalFree(global);
        g_screenshot_state.last_result = hr;
        return false;
    }

    Gdiplus::Bitmap bitmap(stream);
    const bool ok = copy_bitmap_to_bgr_state(bitmap);
    stream->Release();
    return ok;
}

} // namespace

ScreenshotState& screenshot_state() {
    return g_screenshot_state;
}

void RequestScreenshotCapture() {
    g_screenshot_state.capture_requested = true;
}

void SetContinuousScreenshotCapture(bool enabled) {
    g_screenshot_state.continuous_capture = enabled;
}

bool ShouldCaptureScreenshot() {
    return g_screenshot_state.capture_requested || g_screenshot_state.continuous_capture;
}

void ClearOneShotScreenshotRequest() {
    g_screenshot_state.capture_requested = false;
}

void HandleBackBufferToBgr24Conversion(const u16* pixels, std::ptrdiff_t stride_words,
    u32 width, u32 height, bool pixel_mode_555, std::vector<u8>& out) {
    out.assign(static_cast<std::size_t>(width) * height * 3, 0);
    if (pixels == nullptr || width == 0 || height == 0) {
        return;
    }

    for (u32 y = 0; y < height; ++y) {
        const u16* row = pixels + static_cast<std::ptrdiff_t>(y) * stride_words;
        u8* dest = out.data() + static_cast<std::size_t>(y) * width * 3;
        for (u32 x = 0; x < width; ++x) {
            const u16 pixel = row[x];
            if (!pixel_mode_555) {
                dest[x * 3 + 0] = static_cast<u8>((pixel & kBlueMask) << 3);
                dest[x * 3 + 1] = static_cast<u8>((pixel & kGreen565Mask) >> 3);
                dest[x * 3 + 2] = static_cast<u8>((pixel & kRed565Mask) >> 8);
            }
            else {
                dest[x * 3 + 0] = static_cast<u8>((pixel & kBlueMask) << 3);
                dest[x * 3 + 1] = static_cast<u8>((pixel & kGreen555Mask) >> 2);
                dest[x * 3 + 2] = static_cast<u8>((pixel & kRed555Mask) >> 7);
            }
        }
    }
}

bool HandleJpegScreenshotWrite(const char* path, const u8* bgr_pixels, u32 width, u32 height,
    u32 quality) {
    if (path == nullptr || bgr_pixels == nullptr || width == 0 || height == 0) {
        g_screenshot_state.last_result = E_INVALIDARG;
        return false;
    }
    if (!ensure_gdiplus()) {
        return false;
    }

    CLSID jpeg_clsid{};
    if (!get_jpeg_encoder_clsid(jpeg_clsid)) {
        return false;
    }

    const std::wstring wide_path = widen_path(path);
    if (wide_path.empty()) {
        g_screenshot_state.last_result = E_INVALIDARG;
        return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3;
    const std::size_t stride_bytes = (row_bytes + 3u) & ~std::size_t{3u};
    std::vector<u8> padded_pixels;
    const u8* bitmap_pixels = bgr_pixels;
    if (stride_bytes != row_bytes) {
        padded_pixels.assign(stride_bytes * height, 0);
        for (u32 y = 0; y < height; ++y) {
            std::memcpy(padded_pixels.data() + static_cast<std::size_t>(y) * stride_bytes,
                bgr_pixels + static_cast<std::size_t>(y) * row_bytes, row_bytes);
        }
        bitmap_pixels = padded_pixels.data();
    }

    const INT stride = static_cast<INT>(stride_bytes);
    Gdiplus::Bitmap bitmap(static_cast<INT>(width), static_cast<INT>(height), stride,
        PixelFormat24bppRGB, const_cast<BYTE*>(bitmap_pixels));

    ULONG encoder_quality = std::clamp<u32>(quality, 1, 100);
    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &encoder_quality;

    const Gdiplus::Status status = bitmap.Save(wide_path.c_str(), &jpeg_clsid, &params);
    g_screenshot_state.last_result = status == Gdiplus::Ok ? S_OK : E_FAIL;
    if (status == Gdiplus::Ok) {
        g_screenshot_state.last_path = path;
    }
    return status == Gdiplus::Ok;
}

bool HandleNextScreenshotCapture() {
    const std::string path = next_available_screenshot_path();
    const auto& dd = direct_draw_state();
    if (!dd.active || dd.back_surface == nullptr || dd.width == 0 || dd.height == 0) {
        g_screenshot_state.last_result = DDERR_GENERIC;
        ClearOneShotScreenshotRequest();
        return false;
    }

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    HRESULT result = dd.back_surface->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    g_screenshot_state.last_result = result;
    if (FAILED(result)) {
        ClearOneShotScreenshotRequest();
        return false;
    }

    std::vector<u8> bgr;
    const auto* pixels = static_cast<const u16*>(desc.lpSurface);
    HandleBackBufferToBgr24Conversion(pixels, desc.lPitch / static_cast<LONG>(sizeof(u16)),
        dd.width, dd.height, SurfacePixelMode555(), bgr);
    dd.back_surface->Unlock(nullptr);

    const bool ok = HandleJpegScreenshotWrite(path.c_str(), bgr.data(), dd.width, dd.height, 100);
    ClearOneShotScreenshotRequest();
    return ok;
}

bool HandleJpegFileRead(const char* path) {
    if (path == nullptr || !ensure_gdiplus()) {
        g_screenshot_state.last_result = E_INVALIDARG;
        return false;
    }

    const std::wstring wide_path = widen_path(path);
    if (wide_path.empty()) {
        g_screenshot_state.last_result = E_INVALIDARG;
        return false;
    }

    Gdiplus::Bitmap bitmap(wide_path.c_str());
    return copy_bitmap_to_bgr_state(bitmap);
}

bool HandleTrcJpegRecordRead(const char* archive_name, u32 record_index) {
    std::vector<u8> record;
    if (!LoadTrcRecordAlloc(archive_name, record_index, record)) {
        g_screenshot_state.last_result = E_FAIL;
        return false;
    }
    return decode_jpeg_memory_to_bgr_state(record.data(), record.size());
}

void HandleBgr24ToBackBufferConversion(const u8* bgr_pixels, u16* pixels,
    std::ptrdiff_t stride_words, u32 width, u32 height, bool pixel_mode_555) {
    if (bgr_pixels == nullptr || pixels == nullptr) {
        return;
    }

    for (u32 y = 0; y < height; ++y) {
        u16* row = pixels + static_cast<std::ptrdiff_t>(y) * stride_words;
        const u8* src = bgr_pixels + static_cast<std::size_t>(y) * width * 3;
        for (u32 x = 0; x < width; ++x) {
            const u8 blue = src[x * 3 + 0];
            const u8 green = src[x * 3 + 1];
            const u8 red = src[x * 3 + 2];
            if (!pixel_mode_555) {
                row[x] = static_cast<u16>(((static_cast<u16>(red) << 8) & kRed565Mask) |
                    ((static_cast<u16>(green) << 3) & kGreen565Mask) |
                    ((static_cast<u16>(blue) >> 3) & kBlueMask));
            }
            else {
                row[x] = static_cast<u16>(((static_cast<u16>(red) << 7) & kRed555Mask) |
                    ((static_cast<u16>(green) << 2) & kGreen555Mask) |
                    ((static_cast<u16>(blue) >> 3) & kBlueMask));
            }
        }
    }
}

}
#endif
