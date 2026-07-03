#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace ranker {

#ifdef _WIN32
struct ScreenshotState {
    u32 next_index = 0;
    bool capture_requested = false;
    bool continuous_capture = false;
    HRESULT last_result = S_OK;
    std::string last_path;
    u32 image_width = 0;
    u32 image_height = 0;
    u32 image_stride = 0;
    std::vector<u8> bgr_pixels;
};

ScreenshotState& screenshot_state();
void RequestScreenshotCapture();
void SetContinuousScreenshotCapture(bool enabled);
bool ShouldCaptureScreenshot();
void ClearOneShotScreenshotRequest();
void HandleBackBufferToBgr24Conversion(const u16* pixels, std::ptrdiff_t stride_words,
    u32 width, u32 height, bool pixel_mode_555, std::vector<u8>& out);
bool HandleJpegScreenshotWrite(const char* path, const u8* bgr_pixels, u32 width, u32 height,
    u32 quality);
bool HandleNextScreenshotCapture();
bool HandleJpegFileRead(const char* path);
bool HandleTrcJpegRecordRead(const char* archive_name, u32 record_index);
void HandleBgr24ToBackBufferConversion(const u8* bgr_pixels, u16* pixels,
    std::ptrdiff_t stride_words, u32 width, u32 height, bool pixel_mode_555);
#endif

}
