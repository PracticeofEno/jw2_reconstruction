#include "ranker_system_ui.h"

#ifdef _WIN32

#include "ranker_crt_runtime.h"
#include "ranker_frontend_layout.h"

#include <imm.h>

#include <algorithm>
#include <cstring>

namespace ranker {
namespace {

RankerSystemUiState g_system_ui_state;
constexpr int kLegacyBitmapObjectBytes = 0x18;

void record_last_error(bool) {
}

LOGFONTA build_default_font(LONG height) {
    LOGFONTA font{};
    font.lfHeight = height;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfPitchAndFamily = VARIABLE_PITCH;
    return font;
}

void set_owned_disk_root(const char* root_path) {
    auto& disk = g_system_ui_state.disk;
    disk.root_path = root_path == nullptr ? "" : root_path;
    disk.root_path_pointer = root_path == nullptr ? nullptr : disk.root_path.c_str();
}

void set_owned_directory_path(const char* path) {
    auto& directory = g_system_ui_state.directory;
    directory.path = path == nullptr ? "" : path;
    directory.path_pointer = path == nullptr ? nullptr : directory.path.c_str();
}

bool change_or_create_directory(const std::string& path) {
    if (path.empty()) {
        return true;
    }

    if (CrtChangeDirectory(path.c_str()) == 0) {
        return true;
    }

    CrtMakeDirectory(path.c_str());
    return CrtChangeDirectory(path.c_str()) == 0;
}

} // namespace

RankerSystemUiState& system_ui_state() {
    return g_system_ui_state;
}

void SetDiskFreeSpaceRoot(const char* root_path) {
    set_owned_disk_root(root_path);
}

void SetDiskFreeSpaceRootPointer(const char* root_path) {
    auto& disk = g_system_ui_state.disk;
    disk.root_path = root_path == nullptr ? "" : root_path;
    disk.root_path_pointer = root_path;
}

bool GetLegacyDiskFreeSpace() {
    auto& disk = g_system_ui_state.disk;
    const char* root = disk.root_path_pointer;
    const BOOL ok = GetDiskFreeSpaceA(root, &disk.sectors_per_cluster,
        &disk.bytes_per_sector, &disk.free_clusters, &disk.total_clusters);
    disk.query_ok = ok != FALSE;
    record_last_error(disk.query_ok);
    return disk.query_ok;
}

void SetDirectoryEnsurePath(const char* path) {
    auto& directory = g_system_ui_state.directory;
    set_owned_directory_path(path);
    directory.failed = false;
}

void SetDirectoryEnsurePathPointer(const char* path) {
    auto& directory = g_system_ui_state.directory;
    directory.path = path == nullptr ? "" : path;
    directory.path_pointer = path;
    directory.failed = false;
}

bool HandleDirectoryTreeEnsure() {
    auto& directory = g_system_ui_state.directory;
    directory.failed = false;

    const std::string path =
        directory.path_pointer == nullptr ? "" : directory.path_pointer;
    if (path.empty()) {
        record_last_error(true);
        return true;
    }

    std::size_t search_from = 0;
    std::string last_target;
    for (;;) {
        const std::size_t slash = path.find('\\', search_from);
        if (slash == std::string::npos) {
            break;
        }

        const std::string prefix = path.substr(0, slash + 1);
        last_target = prefix;
        if (!change_or_create_directory(prefix)) {
            directory.failed = true;
            return false;
        }
        search_from = slash + 1;
    }

    if (last_target != path && !change_or_create_directory(path)) {
        directory.failed = true;
        return false;
    }
    return true;
}

bool DrawCompatibleBitmap(HDC target_dc, HBITMAP bitmap, i16 x, i16 y) {
    HDC memory_dc = CreateCompatibleDC(target_dc);
    SelectObject(memory_dc, bitmap);
    const int map_mode = GetMapMode(target_dc);
    SetMapMode(memory_dc, map_mode);

    BITMAP bitmap_info;
    std::memset(&bitmap_info, 0xcc, sizeof(bitmap_info));
    GetObjectA(bitmap, kLegacyBitmapObjectBytes, &bitmap_info);

    POINT size{bitmap_info.bmWidth, bitmap_info.bmHeight};
    POINT origin{0, 0};
    DPtoLP(target_dc, &size, 1);
    DPtoLP(target_dc, &origin, 1);

    const BOOL ok = BitBlt(target_dc, x, y, size.x, size.y, memory_dc,
        origin.x, origin.y, SRCCOPY);
    DeleteDC(memory_dc);
    record_last_error(ok != FALSE);
    return ok != FALSE;
}

int MeasureGdiTextWidth(HDC dc, const char* text) {
    auto& state = g_system_ui_state;
    std::memset(&state.last_text_extent, 0xcc, sizeof(state.last_text_extent));

    const int length = static_cast<int>(std::strlen(text));
    const BOOL ok = GetTextExtentPoint32A(dc, text, length, &state.last_text_extent);
    record_last_error(ok != FALSE);
    return state.last_text_extent.cx;
}

void InitializeUiFontHandles() {
    auto& fonts = g_system_ui_state.fonts;

    const std::array<LONG, kRankerUiFontCount> heights{-11, -12, -14, -16, -18};
    for (u32 i = 0; i < kRankerUiFontCount; ++i) {
        fonts.definitions[i] = build_default_font(heights[i]);
        fonts.handles[i] = CreateFontIndirectA(&fonts.definitions[i]);
        if (fonts.handles[i] == nullptr) {
            record_last_error(false);
        }
    }
}

void ShutdownUiFontHandles() {
    auto& fonts = g_system_ui_state.fonts;
    for (HFONT& font : fonts.handles) {
        DeleteObject(font);
    }
}

HFONT GetUiFontHandle(u32 index) {
    if (index >= kRankerUiFontCount) {
        return nullptr;
    }
    return g_system_ui_state.fonts.handles[index];
}

HFONT CreateScaledFrontendUiFont(u32 index) {
    if (index >= kRankerUiFontCount) {
        return nullptr;
    }

    auto& fonts = g_system_ui_state.fonts;
    LOGFONTA definition = fonts.definitions[index];
    if (definition.lfHeight == 0) {
        HFONT source = fonts.handles[index];
        if (source == nullptr ||
            GetObjectA(source, sizeof(definition), &definition) == 0) {
            definition = build_default_font(-12);
        }
    }

    const LONG legacy_height = definition.lfHeight < 0 ?
        -definition.lfHeight : definition.lfHeight;
    const FrontendLayoutPoint target = FrontendLayoutTargetSize();
    const LONG scaled_height = std::max<LONG>(1,
        ScaleFrontendLayoutValue(legacy_height,
            kLegacyFrontendLayoutHeight, target.y));
    definition.lfHeight = -scaled_height;
    definition.lfWidth = 0;
    return CreateFontIndirectA(&definition);
}

void SetWin32UiFontByHeight(u32 height) {
    auto& fonts = g_system_ui_state.fonts;
    fonts.selected_height = height;
    switch (height) {
    case 10:
        fonts.selected = fonts.handles[0];
        break;
    case 14:
        fonts.selected = fonts.handles[2];
        break;
    case 16:
        fonts.selected = fonts.handles[3];
        break;
    case 18:
        fonts.selected = fonts.handles[4];
        break;
    case 12:
    default:
        fonts.selected = fonts.handles[1];
        break;
    }
}

bool InitializeWin32UiFontMetrics(HDC dc) {
    auto& fonts = g_system_ui_state.fonts;
    if (dc == nullptr) {
        g_system_ui_state.last_error = ERROR_INVALID_PARAMETER;
        return false;
    }
    SetWin32UiFontByHeight(12);

    SelectObject(dc, fonts.selected);
    TEXTMETRICA metrics;
    std::memset(&metrics, 0xcc, sizeof(metrics));
    const BOOL ok = GetTextMetricsA(dc, &metrics);
    record_last_error(ok != FALSE);

    fonts.average_char_width = metrics.tmAveCharWidth;
    fonts.font_height = metrics.tmHeight;
    fonts.metrics_initialized = true;
    return ok != FALSE;
}

int MeasureWin32FontCharacterWidth(HDC dc, UINT ch) {
    auto& fonts = g_system_ui_state.fonts;
    if (dc == nullptr) {
        g_system_ui_state.last_error = ERROR_INVALID_PARAMETER;
        return fonts.last_char_width;
    }

    SelectObject(dc, fonts.selected);
    int width = fonts.last_char_width;
    const BOOL ok = GetCharWidthA(dc, ch, ch, &width);
    record_last_error(ok != FALSE);
    if (ok != FALSE) {
        fonts.last_char_width = width;
    }
    return fonts.last_char_width;
}

int CalculateWin32FontDbcsRunWidth(HDC dc, const char* text) {
    if (dc == nullptr || text == nullptr) {
        g_system_ui_state.last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }

    int total_width = 0;
    const auto* p = reinterpret_cast<const u8*>(text);
    while (*p != 0) {
        UINT ch = *p++;
        if ((ch & 0x80u) != 0) {
            ch = (ch << 8) | *p++;
        }

        total_width += MeasureWin32FontCharacterWidth(dc, ch);
    }
    record_last_error(true);
    return total_width;
}

SIZE CalculateWin32FontRunExtent(HDC dc, const char* text) {
    auto& state = g_system_ui_state;
    state.last_text_extent = {1, 1};
    if (dc == nullptr || text == nullptr) {
        state.last_error = ERROR_INVALID_PARAMETER;
        return state.last_text_extent;
    }

    auto& fonts = state.fonts;
    SIZE measured_extent;
    std::memset(&measured_extent, 0xcc, sizeof(measured_extent));

    SelectObject(dc, fonts.selected);
    const int length = static_cast<int>(std::strlen(text));
    const BOOL ok = GetTextExtentPoint32A(dc, text, length, &measured_extent);
    state.last_text_extent = measured_extent;
    record_last_error(ok != FALSE);
    return state.last_text_extent;
}

void HandleDefaultMessageBeep() {
    MessageBeep(0);
}

bool RefreshImeConversionOpenStatus(HWND window) {
    auto& ime = g_system_ui_state.ime;
    HIMC context = ImmGetContext(window);

    DWORD conversion = 0;
    DWORD sentence = 0;
    const BOOL ok = ImmGetConversionStatus(context, &conversion, &sentence);
    if (ok != FALSE) {
        ime.saved_conversion_open = (conversion & IME_CMODE_NATIVE) != 0;
    }
    ImmReleaseContext(window, context);
    record_last_error(ok != FALSE);
    return ok != FALSE;
}

void SetImeConversionOpenTarget(bool open) {
    g_system_ui_state.ime.target_conversion_open = open;
}

bool RestoreImeConversionOpenStatus(HWND window) {
    auto& ime = g_system_ui_state.ime;
    HIMC context = ImmGetContext(window);

    DWORD conversion = 0;
    DWORD sentence = 0;
    BOOL ok = ImmGetConversionStatus(context, &conversion, &sentence);
    if (ok != FALSE) {
        if (ime.target_conversion_open) {
            conversion |= IME_CMODE_NATIVE;
        } else {
            conversion &= ~static_cast<DWORD>(IME_CMODE_NATIVE);
        }
        ok = ImmSetConversionStatus(context, conversion, sentence);
    }
    ImmReleaseContext(window, context);
    record_last_error(ok != FALSE);
    return ok != FALSE;
}

void RecordImeCompositionKeyStatus(WPARAM wparam, LPARAM lparam) {
    auto& ime = g_system_ui_state.ime;
    ime.composition_key_active = (static_cast<u32>(lparam) & 8u) != 0;
    ime.lead_byte = static_cast<BYTE>((static_cast<u32>(wparam) >> 8) & 0xffu);
    ime.trail_byte = static_cast<BYTE>(static_cast<u32>(wparam) & 0xffu);
}

bool CheckDbcsLeadByte(UINT code_page, BYTE value) {
    const BOOL lead = IsDBCSLeadByteEx(code_page, value);
    return lead != FALSE;
}

}
#endif
