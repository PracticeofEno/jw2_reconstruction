#include "ranker_win32_compat.h"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>

namespace ranker {
namespace {

constexpr UINT_PTR kFallbackMonitorHandleValue = 0x12340042u;
constexpr int kSmXVirtualScreen = 0x4c;
constexpr int kSmYVirtualScreen = 0x4d;
constexpr int kSmCxVirtualScreen = 0x4e;
constexpr int kSmCyVirtualScreen = 0x4f;
constexpr int kSmCMonitors = 0x50;
constexpr int kSmSameDisplayFormat = 0x51;

struct User32MonitorThunks {
    using GetSystemMetricsFn = int (WINAPI*)(int);
    using MonitorFromWindowFn = HMONITOR (WINAPI*)(HWND, DWORD);
    using MonitorFromRectFn = HMONITOR (WINAPI*)(LPCRECT, DWORD);
    using MonitorFromPointFn = HMONITOR (WINAPI*)(POINT, DWORD);
    using GetMonitorInfoAFn = BOOL (WINAPI*)(HMONITOR, LPMONITORINFO);
    using EnumDisplayMonitorsFn = BOOL (WINAPI*)(HDC, LPCRECT, MONITORENUMPROC, LPARAM);

    GetSystemMetricsFn get_system_metrics = nullptr;
    MonitorFromWindowFn monitor_from_window = nullptr;
    MonitorFromRectFn monitor_from_rect = nullptr;
    MonitorFromPointFn monitor_from_point = nullptr;
    GetMonitorInfoAFn get_monitor_info_a = nullptr;
    EnumDisplayMonitorsFn enum_display_monitors = nullptr;
};

struct MbcsCodePageState {
    UINT code_page = 0;
    UINT locale_id = 0;
    bool mbcs_enabled = false;
    bool initialized = false;
    std::array<unsigned char, 0x101> type_flags{};
    std::array<unsigned char, 0x100> case_map{};
    std::array<WORD, 6> lead_byte_pairs{};
};

User32MonitorApiState g_monitor_api_state;
User32MonitorThunks g_monitor_thunks;
MbcsCodePageState g_mbcs_code_page;

HMONITOR fallback_monitor_handle() {
    return reinterpret_cast<HMONITOR>(kFallbackMonitorHandleValue);
}

template <typename T>
T proc(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

bool point_inside_primary(POINT point) {
    return point.x >= 0 && point.y >= 0 &&
        point.x < GetSystemMetrics(SM_CXSCREEN) &&
        point.y < GetSystemMetrics(SM_CYSCREEN);
}

bool rect_intersects_primary(const RECT& rect) {
    if (rect.right < 1 || rect.bottom < 1) {
        return false;
    }
    if (rect.left >= GetSystemMetrics(SM_CXSCREEN) ||
        rect.top >= GetSystemMetrics(SM_CYSCREEN)) {
        return false;
    }
    return true;
}

RECT primary_monitor_rect() {
    RECT rect{};
    rect.right = GetSystemMetrics(SM_CXSCREEN);
    rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    return rect;
}

std::size_t mbcs_char_bytes(const char* text) {
    if (text == nullptr || *text == '\0') {
        return 0;
    }
    const char* next = CharNextA(text);
    const auto bytes = static_cast<std::size_t>(next - text);
    return bytes == 0 ? 1 : bytes;
}

unsigned mbcs_code_at(const char* text, std::size_t* bytes_out = nullptr) {
    std::size_t bytes = mbcs_char_bytes(text);
    unsigned code = 0;
    if (bytes >= 2 && text[1] != '\0') {
        bytes = 2;
        code = (static_cast<unsigned char>(text[0]) << 8) |
            static_cast<unsigned char>(text[1]);
    } else if (bytes != 0) {
        bytes = 1;
        code = static_cast<unsigned char>(text[0]);
    }
    if (bytes_out != nullptr) {
        *bytes_out = bytes;
    }
    return code;
}

UINT resolve_requested_code_page(UINT code_page) {
    switch (code_page) {
    case 0xfffffffeu:
        return GetOEMCP();
    case 0xfffffffdu:
        return GetACP();
    case 0xfffffffcu:
        return CP_THREAD_ACP;
    default:
        return code_page;
    }
}

void reset_mbcs_code_page_state() {
    g_mbcs_code_page.code_page = 0;
    g_mbcs_code_page.locale_id = 0;
    g_mbcs_code_page.mbcs_enabled = false;
    g_mbcs_code_page.type_flags.fill(0);
    g_mbcs_code_page.case_map.fill(0);
    g_mbcs_code_page.lead_byte_pairs.fill(0);
}

void mark_lead_byte_range(BYTE first, BYTE last) {
    if (first == 0 || last == 0 || first > last) {
        return;
    }
    for (unsigned value = first; value <= last; ++value) {
        g_mbcs_code_page.type_flags[value + 1] |= 0x04;
    }
}

} // namespace

LPSTR ConvertWideToAnsiDefaultCodePage(LPSTR destination, LPCWSTR source,
    int destination_chars) {
    return ConvertWideToAnsiCodePage(destination, source, destination_chars, 0);
}

LPSTR ConvertWideToAnsiCodePage(LPSTR destination, LPCWSTR source,
    int destination_chars, UINT code_page) {
    if (destination == nullptr || source == nullptr || destination_chars <= 0) {
        return destination;
    }
    destination[0] = '\0';
    WideCharToMultiByte(code_page, 0, source, -1, destination, destination_chars,
        nullptr, nullptr);
    return destination;
}

int MeasureWideStringLength(LPCWSTR source) {
    return source == nullptr ? 0 : lstrlenW(source);
}

LPWSTR ConvertAnsiToWideDefaultCodePage(LPWSTR destination, LPCSTR source,
    int destination_chars) {
    return ConvertAnsiToWideCodePage(destination, source, destination_chars, 0);
}

LPWSTR ConvertAnsiToWideCodePage(LPWSTR destination, LPCSTR source,
    int destination_chars, UINT code_page) {
    if (destination == nullptr || source == nullptr || destination_chars <= 0) {
        return destination;
    }
    destination[0] = L'\0';
    MultiByteToWideChar(code_page, 0, source, -1, destination, destination_chars);
    return destination;
}

DEVMODEW* ConvertDevModeAToW(DEVMODEW* destination, const DEVMODEA* source,
    UINT code_page) {
    if (source == nullptr) {
        return nullptr;
    }
    if (destination == nullptr) {
        return nullptr;
    }

    std::memset(destination, 0, sizeof(*destination));
    ConvertAnsiToWideCodePage(destination->dmDeviceName,
        reinterpret_cast<LPCSTR>(source->dmDeviceName), CCHDEVICENAME, code_page);
    destination->dmSpecVersion = source->dmSpecVersion;
    destination->dmDriverVersion = source->dmDriverVersion;
    destination->dmFields = source->dmFields;
    destination->dmPosition = source->dmPosition;
    destination->dmDisplayOrientation = source->dmDisplayOrientation;
    destination->dmDisplayFixedOutput = source->dmDisplayFixedOutput;
    destination->dmColor = source->dmColor;
    destination->dmDuplex = source->dmDuplex;
    destination->dmYResolution = source->dmYResolution;
    destination->dmTTOption = source->dmTTOption;
    destination->dmCollate = source->dmCollate;
    ConvertAnsiToWideCodePage(destination->dmFormName,
        reinterpret_cast<LPCSTR>(source->dmFormName), CCHFORMNAME, code_page);
    destination->dmLogPixels = source->dmLogPixels;
    destination->dmBitsPerPel = source->dmBitsPerPel;
    destination->dmPelsWidth = source->dmPelsWidth;
    destination->dmPelsHeight = source->dmPelsHeight;
    destination->dmDisplayFlags = source->dmDisplayFlags;
    destination->dmDisplayFrequency = source->dmDisplayFrequency;
    destination->dmICMMethod = source->dmICMMethod;
    destination->dmICMIntent = source->dmICMIntent;
    destination->dmMediaType = source->dmMediaType;
    destination->dmDitherType = source->dmDitherType;
    destination->dmReserved1 = source->dmReserved1;
    destination->dmReserved2 = source->dmReserved2;
    destination->dmPanningWidth = source->dmPanningWidth;
    destination->dmPanningHeight = source->dmPanningHeight;
    destination->dmSize = sizeof(DEVMODEW);
    destination->dmDriverExtra = source->dmDriverExtra;

    if (source->dmDriverExtra != 0) {
        const auto* extra = reinterpret_cast<const u8*>(source) + source->dmSize;
        auto* out_extra = reinterpret_cast<u8*>(destination) + destination->dmSize;
        std::memcpy(out_extra, extra, source->dmDriverExtra);
    }
    return destination;
}

DEVMODEA* ConvertDevModeWToA(DEVMODEA* destination, const DEVMODEW* source,
    UINT code_page) {
    if (source == nullptr) {
        return nullptr;
    }
    if (destination == nullptr) {
        return nullptr;
    }

    std::memset(destination, 0, sizeof(*destination));
    ConvertWideToAnsiCodePage(reinterpret_cast<LPSTR>(destination->dmDeviceName),
        source->dmDeviceName, CCHDEVICENAME, code_page);
    destination->dmSpecVersion = source->dmSpecVersion;
    destination->dmDriverVersion = source->dmDriverVersion;
    destination->dmFields = source->dmFields;
    destination->dmPosition = source->dmPosition;
    destination->dmDisplayOrientation = source->dmDisplayOrientation;
    destination->dmDisplayFixedOutput = source->dmDisplayFixedOutput;
    destination->dmColor = source->dmColor;
    destination->dmDuplex = source->dmDuplex;
    destination->dmYResolution = source->dmYResolution;
    destination->dmTTOption = source->dmTTOption;
    destination->dmCollate = source->dmCollate;
    ConvertWideToAnsiCodePage(reinterpret_cast<LPSTR>(destination->dmFormName),
        source->dmFormName, CCHFORMNAME, code_page);
    destination->dmLogPixels = source->dmLogPixels;
    destination->dmBitsPerPel = source->dmBitsPerPel;
    destination->dmPelsWidth = source->dmPelsWidth;
    destination->dmPelsHeight = source->dmPelsHeight;
    destination->dmDisplayFlags = source->dmDisplayFlags;
    destination->dmDisplayFrequency = source->dmDisplayFrequency;
    destination->dmICMMethod = source->dmICMMethod;
    destination->dmICMIntent = source->dmICMIntent;
    destination->dmMediaType = source->dmMediaType;
    destination->dmDitherType = source->dmDitherType;
    destination->dmReserved1 = source->dmReserved1;
    destination->dmReserved2 = source->dmReserved2;
    destination->dmPanningWidth = source->dmPanningWidth;
    destination->dmPanningHeight = source->dmPanningHeight;
    destination->dmSize = sizeof(DEVMODEA);
    destination->dmDriverExtra = source->dmDriverExtra;

    if (source->dmDriverExtra != 0) {
        const auto* extra = reinterpret_cast<const u8*>(source) + source->dmSize;
        auto* out_extra = reinterpret_cast<u8*>(destination) + destination->dmSize;
        std::memcpy(out_extra, extra, source->dmDriverExtra);
    }
    return destination;
}

User32MonitorApiState& user32_monitor_api_state() {
    return g_monitor_api_state;
}

bool LoadUser32MonitorApiThunks() {
    if (g_monitor_api_state.initialized) {
        return g_monitor_api_state.available;
    }

    HMODULE user32 = GetModuleHandleA("USER32");
    if (user32 != nullptr) {
        g_monitor_thunks.get_system_metrics =
            proc<User32MonitorThunks::GetSystemMetricsFn>(user32, "GetSystemMetrics");
        g_monitor_thunks.monitor_from_window =
            proc<User32MonitorThunks::MonitorFromWindowFn>(user32, "MonitorFromWindow");
        g_monitor_thunks.monitor_from_rect =
            proc<User32MonitorThunks::MonitorFromRectFn>(user32, "MonitorFromRect");
        g_monitor_thunks.monitor_from_point =
            proc<User32MonitorThunks::MonitorFromPointFn>(user32, "MonitorFromPoint");
        g_monitor_thunks.enum_display_monitors =
            proc<User32MonitorThunks::EnumDisplayMonitorsFn>(user32, "EnumDisplayMonitors");
        g_monitor_thunks.get_monitor_info_a =
            proc<User32MonitorThunks::GetMonitorInfoAFn>(user32, "GetMonitorInfoA");
    }

    g_monitor_api_state.available =
        g_monitor_thunks.get_system_metrics != nullptr &&
        g_monitor_thunks.monitor_from_window != nullptr &&
        g_monitor_thunks.monitor_from_rect != nullptr &&
        g_monitor_thunks.monitor_from_point != nullptr &&
        g_monitor_thunks.enum_display_monitors != nullptr &&
        g_monitor_thunks.get_monitor_info_a != nullptr;
    if (!g_monitor_api_state.available) {
        g_monitor_thunks = {};
    }
    g_monitor_api_state.initialized = true;
    return g_monitor_api_state.available;
}

int CompatGetSystemMetrics(int index) {
    if (LoadUser32MonitorApiThunks()) {
        return g_monitor_thunks.get_system_metrics(index);
    }
    switch (index) {
    case kSmXVirtualScreen:
    case kSmYVirtualScreen:
        return 0;
    case kSmCxVirtualScreen:
        return GetSystemMetrics(SM_CXSCREEN);
    case kSmCyVirtualScreen:
        return GetSystemMetrics(SM_CYSCREEN);
    case kSmCMonitors:
    case kSmSameDisplayFormat:
        return 1;
    default:
        return GetSystemMetrics(index);
    }
}

int xGetSystemMetrics(int index) {
    return CompatGetSystemMetrics(index);
}

HMONITOR CompatMonitorFromPoint(POINT point, DWORD flags) {
    if (LoadUser32MonitorApiThunks()) {
        return g_monitor_thunks.monitor_from_point(point, flags);
    }
    if ((flags & 3u) == 0 && !point_inside_primary(point)) {
        return nullptr;
    }
    return fallback_monitor_handle();
}

HMONITOR CompatMonitorFromRect(const RECT* rect, DWORD flags) {
    if (LoadUser32MonitorApiThunks()) {
        return g_monitor_thunks.monitor_from_rect(rect, flags);
    }
    if (rect == nullptr) {
        return (flags & 3u) == 0 ? nullptr : fallback_monitor_handle();
    }
    if ((flags & 3u) == 0 && !rect_intersects_primary(*rect)) {
        return nullptr;
    }
    return fallback_monitor_handle();
}

HMONITOR CompatMonitorFromWindow(HWND window, DWORD flags) {
    if (LoadUser32MonitorApiThunks()) {
        return g_monitor_thunks.monitor_from_window(window, flags);
    }
    if ((flags & 3u) != 0) {
        return fallback_monitor_handle();
    }

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    BOOL ok = FALSE;
    if (IsIconic(window) != FALSE) {
        ok = GetWindowPlacement(window, &placement);
    } else {
        ok = GetWindowRect(window, &placement.rcNormalPosition);
    }
    if (ok == FALSE) {
        return nullptr;
    }
    return CompatMonitorFromRect(&placement.rcNormalPosition, flags);
}

BOOL CompatGetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info) {
    if (LoadUser32MonitorApiThunks()) {
        return g_monitor_thunks.get_monitor_info_a(monitor, info);
    }
    if (monitor != fallback_monitor_handle() || info == nullptr ||
        info->cbSize < sizeof(MONITORINFO)) {
        return FALSE;
    }

    RECT work{};
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0) == FALSE) {
        return FALSE;
    }
    info->rcMonitor = primary_monitor_rect();
    info->rcWork = work;
    info->dwFlags = MONITORINFOF_PRIMARY;
    if (info->cbSize >= sizeof(MONITORINFOEXA)) {
        auto* ex = reinterpret_cast<MONITORINFOEXA*>(info);
        lstrcpyA(ex->szDevice, "DISPLAY");
    }
    return TRUE;
}

BOOL CompatEnumDisplayMonitors(HDC dc, LPCRECT clip_rect,
    MONITORENUMPROC callback, LPARAM data) {
    if (LoadUser32MonitorApiThunks()) {
        return g_monitor_thunks.enum_display_monitors(dc, clip_rect, callback, data);
    }
    if (callback == nullptr) {
        return FALSE;
    }

    RECT monitor = primary_monitor_rect();
    if (clip_rect != nullptr) {
        RECT clipped{};
        if (IntersectRect(&clipped, &monitor, clip_rect) == FALSE) {
            return TRUE;
        }
        monitor = clipped;
    }

    if (dc != nullptr) {
        RECT clip_box{};
        if (GetClipBox(dc, &clip_box) == ERROR) {
            return FALSE;
        }
        RECT clipped{};
        if (IntersectRect(&clipped, &monitor, &clip_box) == FALSE) {
            return TRUE;
        }
        monitor = clipped;
    }

    return callback(fallback_monitor_handle(), dc, &monitor, data);
}

BOOL xEnumDisplayMonitors(HDC dc, LPCRECT clip_rect, MONITORENUMPROC callback,
    LPARAM data) {
    return CompatEnumDisplayMonitors(dc, clip_rect, callback, data);
}

const char* ReturnCStringBufferPointer(const char* value) {
    return value;
}

char* GetCStringDataBufferFromHeader(void* header) {
    return header == nullptr ? nullptr : static_cast<char*>(header) + 0x0c;
}

const char* FindCStringOneOf(const char* text, const char* chars) {
    if (text == nullptr || chars == nullptr) {
        return nullptr;
    }
    return std::strpbrk(text, chars);
}

char* LowercaseCStringInPlace(char* text) {
    if (text != nullptr) {
        CharLowerA(text);
    }
    return text;
}

char* ReverseMbcsStringInPlace(char* text) {
    return ReverseMbcsStringInPlaceCore(text);
}

const char* AdvanceMbcsStringPointer(const char* text) {
    return text == nullptr ? nullptr : CharNextA(text);
}

std::size_t SpanMbcsStringIncluding(const char* text, const char* chars) {
    if (text == nullptr || chars == nullptr) {
        return 0;
    }
    const char* cursor = text;
    while (*cursor != '\0') {
        bool found = false;
        for (const char* needle = chars; *needle != '\0'; needle = CharNextA(needle)) {
            const char* next_needle = CharNextA(needle);
            const char* next_cursor = CharNextA(cursor);
            const auto needle_bytes = static_cast<std::size_t>(next_needle - needle);
            const auto cursor_bytes = static_cast<std::size_t>(next_cursor - cursor);
            if (needle_bytes == cursor_bytes &&
                std::memcmp(needle, cursor, needle_bytes) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }
        cursor = CharNextA(cursor);
    }
    return static_cast<std::size_t>(cursor - text);
}

std::size_t SpanMbcsStringExcluding(const char* text, const char* chars) {
    if (text == nullptr || chars == nullptr) {
        return 0;
    }
    const char* cursor = text;
    while (*cursor != '\0') {
        bool found = false;
        for (const char* needle = chars; *needle != '\0'; needle = CharNextA(needle)) {
            const char* next_needle = CharNextA(needle);
            const char* next_cursor = CharNextA(cursor);
            const auto needle_bytes = static_cast<std::size_t>(next_needle - needle);
            const auto cursor_bytes = static_cast<std::size_t>(next_cursor - cursor);
            if (needle_bytes == cursor_bytes &&
                std::memcmp(needle, cursor, needle_bytes) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        cursor = CharNextA(cursor);
    }
    return static_cast<std::size_t>(cursor - text);
}

int CompareMbcsStringN(const char* lhs, const char* rhs, std::size_t count) {
    if (count == 0 || lhs == rhs) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }

    std::size_t consumed = 0;
    while (consumed < count) {
        const char* lhs_next = CharNextA(lhs);
        const char* rhs_next = CharNextA(rhs);
        auto lhs_len = static_cast<std::size_t>(lhs_next - lhs);
        auto rhs_len = static_cast<std::size_t>(rhs_next - rhs);
        if (consumed + lhs_len > count) {
            lhs_len = 0;
        }
        if (consumed + rhs_len > count) {
            rhs_len = 0;
        }

        unsigned lhs_code = 0;
        unsigned rhs_code = 0;
        if (lhs_len == 1) {
            lhs_code = static_cast<unsigned char>(lhs[0]);
        } else if (lhs_len >= 2) {
            lhs_code = (static_cast<unsigned char>(lhs[0]) << 8) |
                static_cast<unsigned char>(lhs[1]);
        }
        if (rhs_len == 1) {
            rhs_code = static_cast<unsigned char>(rhs[0]);
        } else if (rhs_len >= 2) {
            rhs_code = (static_cast<unsigned char>(rhs[0]) << 8) |
                static_cast<unsigned char>(rhs[1]);
        }
        if (lhs_code != rhs_code) {
            return lhs_code < rhs_code ? -1 : 1;
        }
        if (lhs_code == 0) {
            return 0;
        }
        consumed += std::max(lhs_len, rhs_len);
        lhs = lhs_next;
        rhs = rhs_next;
    }
    return 0;
}

int CompareMbcsCaseInsensitive(const char* lhs, const char* rhs) {
    if (lhs == rhs) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }
    return lstrcmpiA(lhs, rhs);
}

char* CopyMbcsStringNBytes(char* destination, const char* source, std::size_t count) {
    if (destination == nullptr || source == nullptr) {
        return destination;
    }

    char* out = destination;
    const char* in = source;
    std::size_t remaining = count;
    while (remaining != 0 && *in != '\0') {
        std::size_t bytes = 0;
        mbcs_code_at(in, &bytes);
        if (bytes >= 2) {
            if (remaining == 1) {
                *out++ = '\0';
                --remaining;
                break;
            }
            *out++ = *in++;
            *out++ = *in++;
            remaining -= 2;
        } else {
            *out++ = *in++;
            --remaining;
        }
    }
    while (remaining != 0) {
        *out++ = '\0';
        --remaining;
    }
    return destination;
}

char* FindMbcsCharacter(char* text, unsigned character) {
    return const_cast<char*>(FindMbcsCharacter(static_cast<const char*>(text), character));
}

const char* FindMbcsCharacter(const char* text, unsigned character) {
    if (text == nullptr) {
        return nullptr;
    }
    const char* cursor = text;
    while (*cursor != '\0') {
        std::size_t bytes = 0;
        const unsigned code = mbcs_code_at(cursor, &bytes);
        if (code == character) {
            return cursor;
        }
        cursor += bytes == 0 ? 1 : bytes;
    }
    return character == 0 ? cursor : nullptr;
}

int CompareMbcsString(const char* lhs, const char* rhs) {
    if (lhs == rhs) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }
    while (true) {
        std::size_t lhs_bytes = 0;
        std::size_t rhs_bytes = 0;
        const unsigned lhs_code = mbcs_code_at(lhs, &lhs_bytes);
        const unsigned rhs_code = mbcs_code_at(rhs, &rhs_bytes);
        if (lhs_code != rhs_code) {
            return lhs_code < rhs_code ? -1 : 1;
        }
        if (lhs_code == 0) {
            return 0;
        }
        lhs += lhs_bytes == 0 ? 1 : lhs_bytes;
        rhs += rhs_bytes == 0 ? 1 : rhs_bytes;
    }
}

char* FindLastMbcsCharacter(char* text, unsigned character) {
    return const_cast<char*>(FindLastMbcsCharacter(static_cast<const char*>(text), character));
}

const char* FindLastMbcsCharacter(const char* text, unsigned character) {
    if (text == nullptr) {
        return nullptr;
    }
    const char* result = nullptr;
    const char* cursor = text;
    while (*cursor != '\0') {
        std::size_t bytes = 0;
        const unsigned code = mbcs_code_at(cursor, &bytes);
        if (code == character) {
            result = cursor;
        }
        cursor += bytes == 0 ? 1 : bytes;
    }
    return character == 0 ? cursor : result;
}

char* TokenizeMbcsString(char* text, const char* delimiters) {
    thread_local char* next_token = nullptr;
    char* cursor = text != nullptr ? text : next_token;
    if (cursor == nullptr || delimiters == nullptr) {
        return nullptr;
    }

    cursor += SpanMbcsStringIncluding(cursor, delimiters);
    if (*cursor == '\0') {
        next_token = nullptr;
        return nullptr;
    }

    char* token = cursor;
    cursor += SpanMbcsStringExcluding(cursor, delimiters);
    if (*cursor == '\0') {
        next_token = nullptr;
    } else {
        std::size_t bytes = 0;
        mbcs_code_at(cursor, &bytes);
        if (bytes >= 2) {
            cursor[0] = '\0';
            cursor[1] = '\0';
            next_token = cursor + 2;
        } else {
            *cursor = '\0';
            next_token = cursor + 1;
        }
    }
    return token;
}

char* LowercaseMbcsStringInPlace(char* text) {
    if (text != nullptr) {
        CharLowerA(text);
    }
    return text;
}

char* FindMbcsSubstring(char* text, const char* needle) {
    return const_cast<char*>(FindMbcsSubstring(static_cast<const char*>(text), needle));
}

const char* FindMbcsSubstring(const char* text, const char* needle) {
    if (text == nullptr || needle == nullptr) {
        return nullptr;
    }
    if (*needle == '\0') {
        return text;
    }

    const std::size_t text_length = std::strlen(text);
    const std::size_t needle_length = std::strlen(needle);
    if (needle_length > text_length) {
        return nullptr;
    }

    const char* cursor = text;
    const char* last = text + (text_length - needle_length);
    while (*cursor != '\0' && cursor <= last) {
        if (std::memcmp(cursor, needle, needle_length) == 0) {
            return cursor;
        }
        const char* next = CharNextA(cursor);
        cursor = next == cursor ? cursor + 1 : next;
    }
    return nullptr;
}

char* FindMbcsStringOneOf(char* text, const char* characters) {
    return const_cast<char*>(FindMbcsStringOneOf(
        static_cast<const char*>(text), characters));
}

const char* FindMbcsStringOneOf(const char* text, const char* characters) {
    if (text == nullptr || characters == nullptr) {
        return nullptr;
    }
    for (const char* cursor = text; *cursor != '\0';) {
        std::size_t cursor_bytes = 0;
        const unsigned cursor_code = mbcs_code_at(cursor, &cursor_bytes);
        for (const char* needle = characters; *needle != '\0';) {
            std::size_t needle_bytes = 0;
            const unsigned needle_code = mbcs_code_at(needle, &needle_bytes);
            if (cursor_code == needle_code) {
                return cursor;
            }
            needle += needle_bytes == 0 ? 1 : needle_bytes;
        }
        cursor += cursor_bytes == 0 ? 1 : cursor_bytes;
    }
    return nullptr;
}

char* UppercaseMbcsStringInPlace(char* text) {
    if (text != nullptr) {
        CharUpperA(text);
    }
    return text;
}

char* ReverseMbcsStringInPlaceCore(char* text) {
    if (text == nullptr) {
        return nullptr;
    }
    char* cursor = text;
    while (*cursor != '\0') {
        const char* next = CharNextA(cursor);
        if (next - cursor > 1 && cursor[1] != '\0') {
            std::swap(cursor[0], cursor[1]);
            cursor += 2;
        } else {
            ++cursor;
        }
    }
    std::reverse(text, text + lstrlenA(text));
    return text;
}

int GetMbcsCharacterLength(const char* text) {
    if (text == nullptr || *text == '\0') {
        return 0;
    }
    return static_cast<int>(CharNextA(text) - text);
}

const char* PreviousMbcsStringPointer(const char* start, const char* current) {
    if (start == nullptr || current == nullptr || current <= start) {
        return start;
    }
    return CharPrevA(start, current);
}

const char* CrtMbsInc(const char* text) {
    return AdvanceMbcsStringPointer(text);
}

bool CrtIsDigit(unsigned character) {
    if (character < 0x100) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
    }
    return false;
}

bool CrtIsSpace(unsigned character) {
    if (character < 0x100) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    }
    return false;
}

unsigned LocaleIdFromMbcsCodePage(unsigned code_page) {
    switch (code_page) {
    case 932:
        return 0x0411;
    case 936:
        return 0x0804;
    case 949:
        return 0x0412;
    case 950:
        return 0x0404;
    default:
        return 0;
    }
}

int SetMbcsCodePage(unsigned code_page) {
    const UINT resolved_code_page = resolve_requested_code_page(code_page);
    if (resolved_code_page == g_mbcs_code_page.code_page) {
        return 0;
    }

    if (resolved_code_page == 0) {
        reset_mbcs_code_page_state();
        RebuildMbcsCaseMapTables();
        return 0;
    }

    CPINFO info{};
    if (GetCPInfo(resolved_code_page, &info) == FALSE) {
        reset_mbcs_code_page_state();
        RebuildMbcsCaseMapTables();
        return -1;
    }

    reset_mbcs_code_page_state();
    g_mbcs_code_page.code_page = resolved_code_page;
    g_mbcs_code_page.locale_id = LocaleIdFromMbcsCodePage(resolved_code_page);
    if (info.MaxCharSize >= 2) {
        g_mbcs_code_page.mbcs_enabled = true;
        std::size_t pair_index = 0;
        for (const BYTE* range = info.LeadByte;
             range[0] != 0 && range[1] != 0;
             range += 2) {
            mark_lead_byte_range(range[0], range[1]);
            if (pair_index < g_mbcs_code_page.lead_byte_pairs.size()) {
                g_mbcs_code_page.lead_byte_pairs[pair_index++] =
                    static_cast<WORD>((range[0] << 8) | range[1]);
            }
        }
        for (unsigned value = 1; value < 0xff; ++value) {
            g_mbcs_code_page.type_flags[value + 1] |= 0x08;
        }
    }
    RebuildMbcsCaseMapTables();
    return 0;
}

void RebuildMbcsCaseMapTables() {
    for (unsigned value = 0; value < g_mbcs_code_page.case_map.size(); ++value) {
        const auto ch = static_cast<unsigned char>(value);
        g_mbcs_code_page.case_map[value] = 0;
        if (std::isupper(ch) != 0) {
            g_mbcs_code_page.type_flags[value + 1] |= 0x10;
            g_mbcs_code_page.case_map[value] =
                static_cast<unsigned char>(std::tolower(ch));
        } else if (std::islower(ch) != 0) {
            g_mbcs_code_page.type_flags[value + 1] |= 0x20;
            g_mbcs_code_page.case_map[value] =
                static_cast<unsigned char>(std::toupper(ch));
        }
    }

    if (g_mbcs_code_page.code_page == 0) {
        return;
    }

    for (unsigned value = 1; value < 0x100; ++value) {
        if ((g_mbcs_code_page.type_flags[value + 1] & 0x04) != 0) {
            continue;
        }
        char lower = static_cast<char>(value);
        char upper = static_cast<char>(value);
        CharLowerBuffA(&lower, 1);
        CharUpperBuffA(&upper, 1);
        if (lower != static_cast<char>(value)) {
            g_mbcs_code_page.type_flags[value + 1] |= 0x10;
            g_mbcs_code_page.case_map[value] =
                static_cast<unsigned char>(lower);
        } else if (upper != static_cast<char>(value)) {
            g_mbcs_code_page.type_flags[value + 1] |= 0x20;
            g_mbcs_code_page.case_map[value] =
                static_cast<unsigned char>(upper);
        }
    }
}

unsigned GetActiveMbcsCodePage() {
    return g_mbcs_code_page.mbcs_enabled ? g_mbcs_code_page.code_page : 0;
}

void InitializeMbcsCodePageOnce() {
    if (!g_mbcs_code_page.initialized) {
        SetMbcsCodePage(0xfffffffdu);
        g_mbcs_code_page.initialized = true;
    }
}

} // namespace ranker

#endif
