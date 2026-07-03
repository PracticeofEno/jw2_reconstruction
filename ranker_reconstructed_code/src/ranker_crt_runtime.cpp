#include "ranker_crt_runtime.h"
#include "ranker_win32_compat.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <malloc.h>
#include <sys/utime.h>
#endif

namespace ranker {
namespace {

struct CrtDebugAllocation {
    std::size_t size = 0;
    int block_type = 1;
    const char* file_name = nullptr;
    int line_number = 0;
    unsigned long sequence = 0;
};

struct CrtDebugAllocationSnapshot {
    void* memory = nullptr;
    CrtDebugAllocation allocation{};
};

class CrtRuntimeLockScope {
public:
    explicit CrtRuntimeLockScope(int lock_index) : lock_index_(lock_index) {
        LockCrtRuntime(lock_index_);
    }

    ~CrtRuntimeLockScope() {
        UnlockCrtRuntime(lock_index_);
    }

    CrtRuntimeLockScope(const CrtRuntimeLockScope&) = delete;
    CrtRuntimeLockScope& operator=(const CrtRuntimeLockScope&) = delete;

private:
    int lock_index_ = 0;
};

int g_crt_debug_flag = 0;
int g_crt_math_error_mode = 0;
unsigned g_crt_report_modes[3] = {1, 1, 1};
std::array<void*, 3> g_crt_report_files{};
CrtDumpClientCallback g_crt_dump_client = nullptr;
CrtReportHookCallback g_crt_report_hook = nullptr;
unsigned g_fpu_status_word = 0;
unsigned g_fpu_control_word = 0x027f;
thread_local unsigned long g_crt_doserrno = 0;
thread_local CrtThreadData g_thread_data;
std::array<std::recursive_mutex, 0x30> g_crt_locks;
std::unordered_map<FILE*, std::vector<char>> g_temporary_stream_buffers;
bool g_time_zone_initialized = false;
long g_time_zone_seconds = 0;
long g_daylight_bias_seconds = 0;
bool g_daylight_savings_enabled = false;
std::string g_standard_time_name = "STD";
std::string g_daylight_time_name = "DST";
CrtNewHandler g_new_handler = nullptr;
std::size_t g_small_block_threshold = 0x3f8;
std::size_t g_lookaside_threshold = 0x780;
std::unordered_map<void*, CrtDebugAllocation> g_debug_allocations;
std::size_t g_debug_current_bytes = 0;
std::size_t g_debug_high_water_bytes = 0;
std::size_t g_debug_total_allocated_bytes = 0;
unsigned long g_debug_allocation_sequence = 0;
long g_debug_break_allocation = -1;
CrtAllocHookCallback g_crt_alloc_hook = nullptr;
std::vector<CrtOnExitFunction> g_onexit_functions;
std::array<CrtSignalHandler, 32> g_signal_handlers{};
int g_floating_point_exception_code = 0;
void* g_exception_pointer = nullptr;
thread_local char* g_strtok_delimiter_context = nullptr;
#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_unhandled_exception_filter = nullptr;
#endif

constexpr std::size_t kCrtMaxHeapRequest = 0xffffffe0u;

struct RuntimeErrorMessage {
    int id;
    const char* text;
};

constexpr RuntimeErrorMessage kRuntimeErrorMessages[] = {
    {2, "R6002\r\n- floating point not loaded\r\n"},
    {8, "R6008\r\n- not enough space for arguments\r\n"},
    {9, "R6009\r\n- not enough space for environment\r\n"},
    {10, "\r\nabnormal program termination\r\n"},
    {16, "R6016\r\n- not enough space for thread data\r\n"},
    {17, "R6017\r\n- unexpected multithread lock error\r\n"},
    {18, "R6018\r\n- unexpected heap error\r\n"},
    {19, "R6019\r\n- unable to open console device\r\n"},
    {24, "R6024\r\n- not enough space for _onexit/atexit table\r\n"},
    {25, "R6025\r\n- pure virtual function call\r\n"},
    {26, "R6026\r\n- not enough space for stdio initialization\r\n"},
    {27, "R6027\r\n- not enough space for lowio initialization\r\n"},
    {28, "R6028\r\n- unable to initialize heap\r\n"},
    {0xfc, "runtime error "},
    {0xff, "\r\n"},
};

bool is_command_space(char ch) {
    return ch == ' ' || ch == '\t';
}

std::size_t command_line_char_bytes(const char* cursor) {
    if (cursor == nullptr || *cursor == '\0') {
        return 0;
    }
#ifdef _WIN32
    const int length = GetMbcsCharacterLength(cursor);
    if (length > 1 && cursor[1] != '\0') {
        return static_cast<std::size_t>(length);
    }
#endif
    return 1;
}

void append_command_line_char(std::string& out, const char*& cursor) {
    const std::size_t bytes = command_line_char_bytes(cursor);
    if (bytes == 0) {
        return;
    }
    out.append(cursor, cursor + bytes);
    cursor += bytes;
}

std::size_t ansi_environment_block_length(const char* block) {
    if (block == nullptr) {
        return 0;
    }
    const char* cursor = block;
    while (*cursor != '\0') {
        cursor += std::strlen(cursor) + 1;
    }
    return static_cast<std::size_t>(cursor - block) + 1;
}

std::size_t wide_environment_block_length(const wchar_t* block) {
    if (block == nullptr) {
        return 0;
    }
    const wchar_t* cursor = block;
    while (*cursor != L'\0') {
        cursor += std::wcslen(cursor) + 1;
    }
    return static_cast<std::size_t>(cursor - block) + 1;
}

std::string locale_info_string(unsigned locale_id, unsigned type,
    const char* fallback = "") {
#ifdef _WIN32
    char buffer[128]{};
    const LCID mapped_locale = locale_id != 0 ? locale_id : LOCALE_USER_DEFAULT;
    if (GetLocaleInfoA(mapped_locale, static_cast<LCTYPE>(type), buffer,
        static_cast<int>(sizeof(buffer))) != 0) {
        return buffer;
    }
#else
    (void)locale_id;
    (void)type;
#endif
    return fallback != nullptr ? std::string(fallback) : std::string();
}

int locale_info_int(unsigned locale_id, unsigned type, int fallback = 0) {
    const std::string text = locale_info_string(locale_id, type, "");
    if (text.empty()) {
        return fallback;
    }
    return std::atoi(text.c_str());
}

std::size_t round_heap_allocation_size(std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    if (size > kCrtMaxHeapRequest) {
        return 0;
    }
    return (size + 0x0f) & ~std::size_t{0x0f};
}

std::size_t round_heap_threshold(std::size_t threshold) {
    if (threshold > kCrtMaxHeapRequest) {
        return kCrtMaxHeapRequest + 1;
    }
    return (threshold + 0x0f) & ~std::size_t{0x0f};
}

std::size_t lookaside_units_to_bytes(unsigned units) {
    if (units == 0) {
        units = 1;
    }
    if (units > kCrtMaxHeapRequest / 0x10) {
        return 0;
    }
    return static_cast<std::size_t>(units) * 0x10;
}

void* retry_realloc_with_new_handler(void* memory, std::size_t size) {
    const std::size_t rounded = round_heap_allocation_size(size);
    if (rounded == 0) {
        return nullptr;
    }
    void* result = std::realloc(memory, rounded);
    while (result == nullptr && g_new_handler != nullptr &&
        g_new_handler(size) != 0) {
        result = std::realloc(memory, rounded);
    }
    return result;
}

char locale_decimal_point_char() {
    const lconv* locale = std::localeconv();
    return locale != nullptr && locale->decimal_point != nullptr &&
        locale->decimal_point[0] != '\0' ? locale->decimal_point[0] : '.';
}

void set_errno_from_math_type(int type) {
    int* err = CrtErrnoPointer();
    if (type == 1) {
        *err = EDOM;
    } else if (type > 1 && type < 4) {
        *err = ERANGE;
    }
}

int math_type_from_exception_flags(int flags) {
    if ((flags & 0x08) != 0) {
        return 3;
    }
    if ((flags & 0x04) != 0) {
        return 2;
    }
    if ((flags & 0x03) != 0) {
        return 1;
    }
    if ((flags & 0x10) != 0) {
        return 4;
    }
    return 0;
}

int map_locale_category(int category) {
    switch (category) {
    case 0:
        return LC_ALL;
    case 1:
        return LC_COLLATE;
    case 2:
        return LC_CTYPE;
    case 3:
        return LC_MONETARY;
    case 4:
        return LC_NUMERIC;
    case 5:
        return LC_TIME;
    default:
        return -1;
    }
}

void copy_path_component(char* destination, const char* begin, const char* end) {
    if (destination == nullptr) {
        return;
    }
    const auto count = static_cast<std::size_t>(end > begin ? end - begin : 0);
    const std::size_t bounded = count < 0xff ? count : 0xff;
#ifdef _WIN32
    CopyMbcsStringNBytes(destination, begin, bounded);
#else
    std::memcpy(destination, begin, bounded);
#endif
    destination[bounded] = '\0';
}

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_before_month(int year, int month) {
    static constexpr int kMonthDays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int days = 0;
    for (int index = 0; index < month; ++index) {
        days += kMonthDays[index];
        if (index == 1 && is_leap_year(year)) {
            ++days;
        }
    }
    return days;
}

std::time_t make_utc_time(const std::tm& value) {
    int year = value.tm_year + 1900;
    int month = value.tm_mon;
    while (month < 0) {
        month += 12;
        --year;
    }
    while (month > 11) {
        month -= 12;
        ++year;
    }

    long long days = 0;
    if (year >= 1970) {
        for (int current = 1970; current < year; ++current) {
            days += is_leap_year(current) ? 366 : 365;
        }
    } else {
        for (int current = year; current < 1970; ++current) {
            days -= is_leap_year(current) ? 366 : 365;
        }
    }
    days += days_before_month(year, month);
    days += value.tm_mday - 1;

    return static_cast<std::time_t>((days * 24 + value.tm_hour) * 3600 +
        value.tm_min * 60 + value.tm_sec);
}

std::string timezone_name_for(const std::tm& value) {
    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), "%Z", &value) != 0) {
        return buffer;
    }
    return value.tm_isdst > 0 ? "DST" : "STD";
}

void append_bounded(std::string& out, const std::string& text) {
    out.append(text);
}

void append_number_unpadded(int value, std::string& out) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    out.append(buffer);
}

const char* safe_locale_name(const std::array<std::string, 7>& values,
    int index) {
    static const std::string empty;
    return index >= 0 && index < static_cast<int>(values.size())
        ? values[static_cast<std::size_t>(index)].c_str()
        : empty.c_str();
}

const char* safe_locale_name(const std::array<std::string, 12>& values,
    int index) {
    static const std::string empty;
    return index >= 0 && index < static_cast<int>(values.size())
        ? values[static_cast<std::size_t>(index)].c_str()
        : empty.c_str();
}

std::string join_locale_pairs(const std::array<std::string, 7>& abbreviated,
    const std::array<std::string, 7>& full) {
    std::string out;
    for (std::size_t index = 0; index < abbreviated.size(); ++index) {
        out.push_back(':');
        out.append(abbreviated[index]);
        out.push_back(':');
        out.append(full[index]);
    }
    return out;
}

std::string join_locale_pairs(const std::array<std::string, 12>& abbreviated,
    const std::array<std::string, 12>& full) {
    std::string out;
    for (std::size_t index = 0; index < abbreviated.size(); ++index) {
        out.push_back(':');
        out.append(abbreviated[index]);
        out.push_back(':');
        out.append(full[index]);
    }
    return out;
}

#ifdef _WIN32
struct DosErrnoMap {
    unsigned long dos_error;
    int crt_errno;
};

constexpr DosErrnoMap kDosErrnoMap[] = {
    {1, EINVAL},
    {2, ENOENT},
    {3, ENOENT},
    {4, EMFILE},
    {5, EACCES},
    {6, EBADF},
    {7, ENOMEM},
    {8, ENOMEM},
    {9, ENOMEM},
    {10, E2BIG},
    {11, ENOEXEC},
    {12, EINVAL},
    {13, EINVAL},
    {15, ENOENT},
    {16, EACCES},
    {17, EXDEV},
    {18, ENOENT},
    {32, EACCES},
    {33, EACCES},
    {53, ENOENT},
    {65, EACCES},
    {67, ENOENT},
    {80, EEXIST},
    {82, EACCES},
    {83, EACCES},
    {87, EINVAL},
    {89, EAGAIN},
    {108, EACCES},
    {109, EPIPE},
    {112, ENOSPC},
    {114, EBADF},
    {128, ECHILD},
    {129, ECHILD},
    {130, EBADF},
    {131, EINVAL},
    {132, EACCES},
    {145, ENOTEMPTY},
    {158, EACCES},
    {161, ENOENT},
    {164, EAGAIN},
    {167, EACCES},
    {183, EEXIST},
    {206, ENOENT},
    {215, EAGAIN},
    {1816, ENOMEM},
};

int errno_from_dos_error(unsigned long error) {
    for (const DosErrnoMap& entry : kDosErrnoMap) {
        if (entry.dos_error == error) {
            return entry.crt_errno;
        }
    }
    if (error >= 0x13 && error <= 0x24) {
        return EACCES;
    }
    if (error >= 0xbc && error <= 0xca) {
        return ENOEXEC;
    }
    return EINVAL;
}

void set_errno_from_win32_error(DWORD error) {
    g_crt_doserrno = error;
    errno = errno_from_dos_error(error);
}

void copy_find_data(const WIN32_FIND_DATAA& source, CrtFindDataA& destination) {
    destination.attrib = source.dwFileAttributes == FILE_ATTRIBUTE_NORMAL
        ? 0
        : source.dwFileAttributes;
    destination.time_create = CrtFileTimeToUnixTime(source.ftCreationTime);
    destination.time_access = CrtFileTimeToUnixTime(source.ftLastAccessTime);
    destination.time_write = CrtFileTimeToUnixTime(source.ftLastWriteTime);
    destination.size = source.nFileSizeLow;
    CrtStrCopy(destination.name, source.cFileName);
}
#endif

} // namespace

void DestroyCrtLocaleObject(void*) {
}

void DeleteCrtLocaleObject(void* object, bool free_storage) {
    DestroyCrtLocaleObject(object);
    if (free_storage) {
        std::free(object);
    }
}

char* CrtStrCopy(char* destination, const char* source) {
    if (destination == nullptr || source == nullptr) {
        return destination;
    }
    return std::strcpy(destination, source);
}

char* CrtStrCat(char* destination, const char* source) {
    if (destination == nullptr || source == nullptr) {
        return destination;
    }
    return std::strcat(destination, source);
}

char* CrtStrStr(char* text, const char* needle) {
    return const_cast<char*>(CrtStrStr(static_cast<const char*>(text), needle));
}

const char* CrtStrStr(const char* text, const char* needle) {
    if (text == nullptr || needle == nullptr) {
        return nullptr;
    }
    return std::strstr(text, needle);
}

int CrtSprintf(char* destination, const char* format, ...) {
    if (destination == nullptr || format == nullptr) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    const int result = std::vsprintf(destination, format, args);
    va_end(args);
    return result;
}

int CrtSnprintf(char* destination, int destination_chars, const char* format, ...) {
    if (destination == nullptr || format == nullptr) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    const int result = std::vsnprintf(destination,
        destination_chars > 0 ? static_cast<std::size_t>(destination_chars) : 0,
        format, args);
    va_end(args);
    if (destination_chars > 0) {
        destination[destination_chars - 1] = '\0';
    }
    return result;
}

int CrtFprintf(FILE* stream, const char* format, ...) {
    if (stream == nullptr || format == nullptr) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    const int result = std::vfprintf(stream, format, args);
    va_end(args);
    return result;
}

FILE* CrtFsopen(const char* path, const char* mode, int) {
    if (path == nullptr || path[0] == '\0' || mode == nullptr || mode[0] == '\0') {
        return nullptr;
    }
    return std::fopen(path, mode);
}

FILE* CrtFopenShare(const char* path, const char* mode, int share_flags) {
    return CrtFsopen(path, mode, share_flags);
}

FILE* CrtFopen(const char* path, const char* mode) {
    return CrtFsopen(path, mode, 0x40);
}

int CrtFclose(FILE* stream) {
    return stream == nullptr ? EOF : std::fclose(stream);
}

std::size_t CrtFread(void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream) {
    return CrtFreadUnlocked(buffer, element_size, element_count, stream);
}

std::size_t CrtFreadUnlocked(void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream) {
    if (buffer == nullptr || stream == nullptr || element_size == 0 ||
        element_count == 0) {
        return 0;
    }
    return std::fread(buffer, element_size, element_count, stream);
}

std::size_t CrtFwrite(const void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream) {
    return CrtFwriteUnlocked(buffer, element_size, element_count, stream);
}

std::size_t CrtFwriteUnlocked(const void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream) {
    if (buffer == nullptr || stream == nullptr || element_size == 0 ||
        element_count == 0) {
        return 0;
    }
    return std::fwrite(buffer, element_size, element_count, stream);
}

long CrtFileDescriptorLength(int file_descriptor) {
#ifdef _WIN32
    const long current = _lseek(file_descriptor, 0, SEEK_CUR);
    if (current < 0) {
        return -1;
    }
    const long end = _lseek(file_descriptor, 0, SEEK_END);
    if (end >= 0 && end != current) {
        _lseek(file_descriptor, current, SEEK_SET);
    }
    return end;
#else
    (void)file_descriptor;
    return -1;
#endif
}

long CrtFtell(FILE* stream) {
    return CrtFtellUnlocked(stream);
}

long CrtFtellUnlocked(FILE* stream) {
    return stream == nullptr ? -1 : std::ftell(stream);
}

int CrtFseek(FILE* stream, long offset, int origin) {
    return CrtFseekUnlocked(stream, offset, origin);
}

int CrtFseekUnlocked(FILE* stream, long offset, int origin) {
    if (stream == nullptr ||
        (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)) {
        errno = EINVAL;
        return -1;
    }
    return std::fseek(stream, offset, origin);
}

#ifdef _WIN32
HANDLE CrtFindFirstFile(const char* pattern, CrtFindDataA& out) {
    WIN32_FIND_DATAA find_data{};
    const HANDLE handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        set_errno_from_win32_error(GetLastError());
        return INVALID_HANDLE_VALUE;
    }
    copy_find_data(find_data, out);
    return handle;
}

int CrtFindNextFile(HANDLE handle, CrtFindDataA& out) {
    WIN32_FIND_DATAA find_data{};
    if (FindNextFileA(handle, &find_data) == FALSE) {
        set_errno_from_win32_error(GetLastError());
        return -1;
    }
    copy_find_data(find_data, out);
    return 0;
}

int CrtFindClose(HANDLE handle) {
    if (FindClose(handle) == FALSE) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

std::time_t CrtFileTimeToUnixTime(const FILETIME& file_time) {
    if (file_time.dwLowDateTime == 0 && file_time.dwHighDateTime == 0) {
        return static_cast<std::time_t>(-1);
    }

    FILETIME local_file_time{};
    SYSTEMTIME local_system_time{};
    if (FileTimeToLocalFileTime(&file_time, &local_file_time) == FALSE ||
        FileTimeToSystemTime(&local_file_time, &local_system_time) == FALSE) {
        return static_cast<std::time_t>(-1);
    }

    std::tm value{};
    value.tm_year = static_cast<int>(local_system_time.wYear) - 1900;
    value.tm_mon = static_cast<int>(local_system_time.wMonth) - 1;
    value.tm_mday = static_cast<int>(local_system_time.wDay);
    value.tm_hour = static_cast<int>(local_system_time.wHour);
    value.tm_min = static_cast<int>(local_system_time.wMinute);
    value.tm_sec = static_cast<int>(local_system_time.wSecond);
    value.tm_isdst = -1;
    return std::mktime(&value);
}
#endif

void CrtStackProbe(std::size_t bytes) {
    volatile std::size_t touched = bytes;
    (void)touched;
}

int CrtAtoi(const char* text) {
    return text == nullptr ? 0 : std::atoi(text);
}

long long CrtAtoi64(const char* text) {
    return text == nullptr ? 0 : std::strtoll(text, nullptr, 10);
}

long CrtStrToLong(const char* text, char** end, int radix) {
    if (text == nullptr) {
        if (end != nullptr) {
            *end = nullptr;
        }
        return 0;
    }
    return std::strtol(text, end, radix);
}

unsigned long CrtStrToUnsignedLong(const char* text, char** end, int radix) {
    if (text == nullptr) {
        if (end != nullptr) {
            *end = nullptr;
        }
        return 0;
    }
    return std::strtoul(text, end, radix);
}

unsigned long CrtStrToLongCore(const char* text, char** end, int radix,
    bool unsigned_result) {
    return unsigned_result
        ? CrtStrToUnsignedLong(text, end, radix)
        : static_cast<unsigned long>(CrtStrToLong(text, end, radix));
}

unsigned CrtRotateLeft32Thunk(unsigned value, unsigned count) {
    return CrtRotateLeft32(value, count);
}

unsigned CrtRotateLeft32(unsigned value, unsigned count) {
    count &= 31u;
    if (count == 0) {
        return value;
    }
    return (value << count) | (value >> (32u - count));
}

void CrtSplitPath(const char* path, char* drive, char* directory,
    char* filename, char* extension) {
    if (drive != nullptr) {
        drive[0] = '\0';
    }
    if (directory != nullptr) {
        directory[0] = '\0';
    }
    if (filename != nullptr) {
        filename[0] = '\0';
    }
    if (extension != nullptr) {
        extension[0] = '\0';
    }
    if (path == nullptr) {
        return;
    }

    const char* cursor = path;
    if (cursor[0] != '\0' && cursor[1] == ':') {
        copy_path_component(drive, cursor, cursor + 2);
        cursor += 2;
    }

    const char* name_begin = cursor;
    const char* dot = nullptr;
    for (const char* scan = cursor; *scan != '\0';) {
#ifdef _WIN32
        const char* next = CharNextA(scan);
        if (next == scan) {
            ++next;
        }
#else
        const char* next = scan + 1;
#endif
        if ((next - scan) == 1) {
            if (*scan == '/' || *scan == '\\') {
                name_begin = next;
                dot = nullptr;
            } else if (*scan == '.') {
                dot = scan;
            }
        }
        scan = next;
    }

    copy_path_component(directory, cursor, name_begin);
    if (dot == nullptr || dot < name_begin) {
        copy_path_component(filename, name_begin, path + std::strlen(path));
    } else {
        copy_path_component(filename, name_begin, dot);
        copy_path_component(extension, dot, path + std::strlen(path));
    }
}

void* CrtMemMoveBytes(void* destination, const void* source, std::size_t size) {
    return std::memmove(destination, source, size);
}

int CrtRemovePath(const char* path) {
    if (path == nullptr) {
        errno = EINVAL;
        return -1;
    }
#ifdef _WIN32
    if (DeleteFileA(path) == FALSE) {
        set_errno_from_win32_error(GetLastError());
        return -1;
    }
    return 0;
#else
    return std::remove(path);
#endif
}

void CrtRemovePathThunk(const char* path) {
    (void)CrtRemovePath(path);
}

void CrtAssertFailed(const char* expression, const char* file, int line) {
    std::fprintf(stderr, "Assertion failed: %s, file %s, line %d\n",
        expression != nullptr ? expression : "<unknown>",
        file != nullptr ? file : "<unknown>", line);
    std::abort();
}

int CrtMakeDirectory(const char* path) {
    if (path == nullptr) {
        errno = EINVAL;
        return -1;
    }
#ifdef _WIN32
    if (CreateDirectoryA(path, nullptr) == FALSE) {
        set_errno_from_win32_error(GetLastError());
        return -1;
    }
    return 0;
#else
    (void)path;
    errno = ENOSYS;
    return -1;
#endif
}

int CrtFileStatusByDescriptor(int file_descriptor, CrtFileStatus& status) {
    status = {};
#ifdef _WIN32
    struct _stat native_status {};
    if (_fstat(file_descriptor, &native_status) != 0) {
        return -1;
    }
#else
    struct stat native_status {};
    if (fstat(file_descriptor, &native_status) != 0) {
        return -1;
    }
#endif
    status.device = static_cast<unsigned>(native_status.st_dev);
    status.inode = static_cast<unsigned>(native_status.st_ino);
    status.mode = static_cast<unsigned>(native_status.st_mode);
    status.link_count = static_cast<unsigned>(native_status.st_nlink);
    status.user_id = static_cast<unsigned>(native_status.st_uid);
    status.group_id = static_cast<unsigned>(native_status.st_gid);
    status.special_device = static_cast<unsigned>(native_status.st_rdev);
    status.size = static_cast<unsigned>(native_status.st_size);
    status.access_time = native_status.st_atime;
    status.modify_time = native_status.st_mtime;
    status.create_time = native_status.st_ctime;
    return 0;
}

void CrtRuntimeErrorExitProcess(int message_id) {
    std::fprintf(stderr, "CRT runtime error: R%04d\n", message_id);
#ifdef _WIN32
    ExitProcess(0xff);
#else
    std::exit(0xff);
#endif
}

int* CrtErrnoPointer() {
    return &errno;
}

unsigned long* CrtDosErrnoPointer() {
    return &g_crt_doserrno;
}

int CrtChangeDirectory(const char* path) {
    if (path == nullptr) {
        errno = EINVAL;
        return -1;
    }
#ifdef _WIN32
    if (SetCurrentDirectoryA(path) == FALSE) {
        set_errno_from_win32_error(GetLastError());
        return -1;
    }
    return 0;
#else
    (void)path;
    errno = ENOSYS;
    return -1;
#endif
}

char* CrtStrChr(char* text, int character) {
    return const_cast<char*>(CrtStrChr(static_cast<const char*>(text), character));
}

const char* CrtStrChr(const char* text, int character) {
    if (text == nullptr) {
        return nullptr;
    }
    return std::strchr(text, character);
}

int UppercaseAsciiFromLowercase(int character) {
    return character - 0x20;
}

int CrtToUpper(int character) {
    return std::toupper(static_cast<unsigned char>(character));
}

int CrtToUpperLocale(int character) {
    return CrtToUpper(character);
}

int CrtVsnprintf(char* destination, int destination_chars,
    const char* format, va_list args) {
    const int result = CrtOutputFormatCore(destination,
        destination_chars > 0 ? static_cast<std::size_t>(destination_chars) : 0,
        format, args);
    if (destination != nullptr && destination_chars > 0) {
        destination[destination_chars - 1] = '\0';
    }
    return result;
}

int CrtVsprintf(char* destination, const char* format, va_list args) {
    if (destination == nullptr || format == nullptr) {
        return -1;
    }
    va_list copy;
    va_copy(copy, args);
    const int result = std::vsprintf(destination, format, copy);
    va_end(copy);
    return result;
}

int CrtOutputFormatCore(char* destination, std::size_t destination_chars,
    const char* format, va_list args) {
    if (destination == nullptr || format == nullptr) {
        return -1;
    }
    va_list copy;
    va_copy(copy, args);
    const int result = std::vsnprintf(destination, destination_chars, format, copy);
    va_end(copy);
    return result;
}

int CrtFlushBufferedCharacter(FILE* stream, int character) {
    if (stream == nullptr) {
        errno = EINVAL;
        return EOF;
    }
    return std::fputc(character, stream);
}

int CrtOutputPutChar(std::string& out, int character) {
    out.push_back(static_cast<char>(character));
    return 1;
}

void CrtOutputRepeatChar(std::string& out, int character, int count) {
    if (count > 0) {
        out.append(static_cast<std::size_t>(count), static_cast<char>(character));
    }
}

void CrtOutputWriteString(std::string& out, const char* text, int count) {
    if (text == nullptr || count <= 0) {
        return;
    }
    out.append(text, text + count);
}

std::uint32_t CrtReadVaArg32(const unsigned char*& cursor) {
    std::uint32_t value = 0;
    if (cursor != nullptr) {
        std::memcpy(&value, cursor, sizeof(value));
        cursor += sizeof(value);
    }
    return value;
}

std::uint64_t CrtReadVaArg64(const unsigned char*& cursor) {
    std::uint64_t value = 0;
    if (cursor != nullptr) {
        std::memcpy(&value, cursor, sizeof(value));
        cursor += sizeof(value);
    }
    return value;
}

std::uint16_t CrtReadVaArgWideChar(const unsigned char*& cursor) {
    std::uint16_t value = 0;
    if (cursor != nullptr) {
        std::memcpy(&value, cursor, sizeof(value));
        cursor += sizeof(std::uint32_t);
    }
    return value;
}

void CrtDebugBreak() {
#ifdef _WIN32
    DebugBreak();
#endif
}

void BreakIfCrtReportRequested(int report_result) {
    if (report_result == 1) {
        CrtDebugBreak();
    }
}

void WriteCrtReportToFile(void* report_file, const std::string& report) {
#ifdef _WIN32
    HANDLE handle = report_file != nullptr
        ? static_cast<HANDLE>(report_file)
        : GetStdHandle(STD_ERROR_HANDLE);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(handle, report.data(), static_cast<DWORD>(report.size()),
            &written, nullptr);
        WriteFile(handle, "\n", 1, &written, nullptr);
        return;
    }
#else
    FILE* stream = report_file != nullptr
        ? static_cast<FILE*>(report_file)
        : stderr;
    if (stream != nullptr) {
        std::fputs(report.c_str(), stream);
        std::fputc('\n', stream);
        return;
    }
#endif
    std::fprintf(stderr, "%s\n", report.c_str());
}

int CrtSetReportMode(int report_type, unsigned mode) {
    if (report_type < 0 || report_type > 2) {
        return -1;
    }
    const int previous = static_cast<int>(g_crt_report_modes[report_type]);
    if (mode != 0xffffffffu && (mode & ~7u) == 0) {
        g_crt_report_modes[report_type] = mode;
    } else if (mode != 0xffffffffu) {
        return -1;
    }
    return previous;
}

void* CrtSetReportFile(int report_type, void* file) {
    if (report_type < 0 || report_type > 2) {
        return reinterpret_cast<void*>(static_cast<std::intptr_t>(-2));
    }
    void* previous = g_crt_report_files[static_cast<std::size_t>(report_type)];
    if (file == reinterpret_cast<void*>(static_cast<std::intptr_t>(-6))) {
        return previous;
    }
#ifdef _WIN32
    if (file == reinterpret_cast<void*>(static_cast<std::intptr_t>(-4))) {
        file = GetStdHandle(STD_OUTPUT_HANDLE);
    } else if (file == reinterpret_cast<void*>(static_cast<std::intptr_t>(-5))) {
        file = GetStdHandle(STD_ERROR_HANDLE);
    }
#endif
    g_crt_report_files[static_cast<std::size_t>(report_type)] = file;
    return previous;
}

CrtDumpClientCallback CrtSetDumpClient(CrtDumpClientCallback callback) {
    CrtDumpClientCallback previous = g_crt_dump_client;
    g_crt_dump_client = callback;
    return previous;
}

CrtReportHookCallback CrtSetReportHook(CrtReportHookCallback callback) {
    CrtReportHookCallback previous = g_crt_report_hook;
    g_crt_report_hook = callback;
    return previous;
}

int CrtDbgReport(int report_type, const char* file, int line,
    const char* module, const char* format, ...) {
    if (report_type < 0 || report_type > 2) {
        return -1;
    }
    char message[2048]{};
    if (format != nullptr) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);
    }
    std::string report = "Debug report type " + std::to_string(report_type);
    if (module != nullptr) {
        report += " module ";
        report += module;
    }
    if (file != nullptr) {
        char location[256]{};
        std::snprintf(location, sizeof(location), " at %s:%d", file, line);
        report += location;
    }
    if (message[0] != '\0') {
        report += ": ";
        report += message;
    }
    if (g_crt_report_hook != nullptr) {
        int hook_result = 0;
        if (g_crt_report_hook(report_type, report.c_str(), &hook_result) != 0) {
            return hook_result;
        }
    }
    const unsigned mode = g_crt_report_modes[static_cast<std::size_t>(report_type)];
    if ((mode & 2U) != 0U) {
#ifdef _WIN32
        std::string debug_report = report;
        debug_report.push_back('\n');
        OutputDebugStringA(debug_report.c_str());
#else
        std::fprintf(stderr, "%s\n", report.c_str());
#endif
    }
    if ((mode & 1U) != 0U) {
        WriteCrtReportToFile(
            g_crt_report_files[static_cast<std::size_t>(report_type)], report);
    }
    if ((mode & 4U) != 0U) {
        char line_text[32]{};
        std::snprintf(line_text, sizeof(line_text), "%d", line);
        if (CrtDbgReportDialog(report_type, file, line_text, module,
                message[0] != '\0' ? message : report.c_str())) {
            return 1;
        }
    }
    return 0;
}

bool CrtDbgReportDialog(int report_type, const char* file, const char* line,
    const char* module, const char* message) {
    std::fprintf(stderr, "Debug report dialog type %d", report_type);
    if (module != nullptr) {
        std::fprintf(stderr, " module %s", module);
    }
    if (file != nullptr) {
        std::fprintf(stderr, " file %s", file);
    }
    if (line != nullptr) {
        std::fprintf(stderr, " line %s", line);
    }
    if (message != nullptr) {
        std::fprintf(stderr, ": %s", message);
    }
    std::fputc('\n', stderr);
    return false;
}

void DestroyTypeInfoObject(void*) {
}

void* DeleteTypeInfoObject(void* object, bool free_storage) {
    DestroyTypeInfoObject(object);
    if (free_storage) {
        std::free(object);
    }
    return object;
}

const char* TypeInfoRawName(const void* object) {
    return object == nullptr ? nullptr :
        reinterpret_cast<const char*>(object) + 8;
}

void* InitializeTypeInfoObject(void* object) {
    return object;
}

void* ReturnTypeInfoObject(void* object) {
    return object;
}

int CrtSwprintf(wchar_t* destination, const wchar_t* format, ...) {
    if (destination == nullptr || format == nullptr) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    const int result = std::vswprintf(destination, 32767, format, args);
    va_end(args);
    return result;
}

#ifdef _WIN32
HANDLE CrtBeginThreadEx(LPSECURITY_ATTRIBUTES security_attributes,
    std::size_t stack_size, CrtThreadProcedure procedure, void* context,
    DWORD creation_flags, LPDWORD thread_id) {
    return CreateThread(security_attributes, stack_size,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(procedure), context,
        creation_flags, thread_id);
}

DWORD WINAPI CrtThreadStartThunk(void*) {
    return 0;
}
#endif

std::size_t CrtWideStringLength(const wchar_t* text) {
    return text == nullptr ? 0 : std::wcslen(text);
}

std::time_t CrtMktime(std::tm* value) {
    return CrtMakeTimeCore(value, true);
}

std::time_t CrtMkGmTime(std::tm* value) {
    return CrtMakeTimeCore(value, false);
}

std::time_t CrtMakeTimeCore(std::tm* value, bool local_time) {
    if (value == nullptr) {
        return static_cast<std::time_t>(-1);
    }
    if (local_time) {
        return std::mktime(value);
    }
    return make_utc_time(*value);
}

const LocaleTimeData& DefaultLocaleTimeData() {
    static const LocaleTimeData data{
        {{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}},
        {{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}},
        {{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}},
        {{"January", "February", "March", "April", "May", "June",
          "July", "August", "September", "October", "November", "December"}},
        "AM",
        "PM",
        "MM/dd/yy",
        "dddd, MMMM dd, yyyy",
        "HH:mm:ss"
    };
    return data;
}

std::string BuildLocaleDayNamesList() {
    const LocaleTimeData& data = DefaultLocaleTimeData();
    return join_locale_pairs(data.abbreviated_day_names, data.day_names);
}

std::string BuildLocaleMonthNamesList() {
    const LocaleTimeData& data = DefaultLocaleTimeData();
    return join_locale_pairs(data.abbreviated_month_names, data.month_names);
}

LocaleTimeData CloneLocaleTimeData(const LocaleTimeData& source) {
    return source;
}

std::size_t CrtStrftime(char* destination, std::size_t destination_chars,
    const char* format, const std::tm* value) {
    return CrtStrftimeWithLocale(destination, destination_chars, format, value, nullptr);
}

std::size_t CrtStrftimeWithLocale(char* destination, std::size_t destination_chars,
    const char* format, const std::tm* value, const LocaleTimeData* locale) {
    if (destination == nullptr || destination_chars == 0 ||
        format == nullptr || value == nullptr) {
        return 0;
    }

    const LocaleTimeData& active_locale =
        locale != nullptr ? *locale : DefaultLocaleTimeData();
    std::string out;
    for (const char* cursor = format; *cursor != '\0'; ++cursor) {
        if (*cursor != '%') {
            out.push_back(*cursor);
        } else {
            bool alternate = false;
            ++cursor;
            if (*cursor == '#') {
                alternate = true;
                ++cursor;
            }
            if (*cursor == '\0') {
                break;
            }
            FormatStrftimeToken(*cursor, *value, out, active_locale, alternate);
        }
        if (out.size() >= destination_chars) {
            destination[0] = '\0';
            return 0;
        }
    }

    if (out.size() >= destination_chars) {
        destination[0] = '\0';
        return 0;
    }
    std::memcpy(destination, out.c_str(), out.size() + 1);
    return out.size();
}

void FormatStrftimeToken(char token, const std::tm& value, std::string& out,
    const LocaleTimeData& locale, bool alternate) {
    switch (token) {
    case '%':
        out.push_back('%');
        break;
    case 'A':
        append_bounded(out, safe_locale_name(locale.day_names, value.tm_wday));
        break;
    case 'B':
        append_bounded(out, safe_locale_name(locale.month_names, value.tm_mon));
        break;
    case 'H':
        StoreStrftimePaddedNumber(value.tm_hour, 2, out, alternate);
        break;
    case 'I': {
        int hour = value.tm_hour % 12;
        if (hour == 0) {
            hour = 12;
        }
        StoreStrftimePaddedNumber(hour, 2, out, alternate);
        break;
    }
    case 'M':
        StoreStrftimePaddedNumber(value.tm_min, 2, out, alternate);
        break;
    case 'S':
        StoreStrftimePaddedNumber(value.tm_sec, 2, out, alternate);
        break;
    case 'U': {
        int week = 0;
        if (value.tm_yday >= value.tm_wday) {
            week = value.tm_yday / 7;
            if (value.tm_wday <= value.tm_yday % 7) {
                ++week;
            }
        }
        StoreStrftimePaddedNumber(week, 2, out, alternate);
        break;
    }
    case 'W': {
        const int monday_index = value.tm_wday == 0 ? 6 : value.tm_wday - 1;
        int week = 0;
        if (value.tm_yday >= monday_index) {
            week = value.tm_yday / 7;
            if (monday_index <= value.tm_yday % 7) {
                ++week;
            }
        }
        StoreStrftimePaddedNumber(week, 2, out, alternate);
        break;
    }
    case 'X':
        FormatLocaleDateTimePattern(locale.time_format.c_str(), value, out, locale);
        break;
    case 'Y':
        StoreStrftimePaddedNumber(value.tm_year + 1900, 4, out, alternate);
        break;
    case 'Z':
    case 'z':
        append_bounded(out, timezone_name_for(value));
        break;
    case 'a':
        append_bounded(out, safe_locale_name(locale.abbreviated_day_names, value.tm_wday));
        break;
    case 'b':
        append_bounded(out, safe_locale_name(locale.abbreviated_month_names, value.tm_mon));
        break;
    case 'c':
        FormatLocaleDateTimePattern(
            alternate ? locale.long_date_format.c_str() : locale.short_date_format.c_str(),
            value, out, locale);
        out.push_back(' ');
        FormatLocaleDateTimePattern(locale.time_format.c_str(), value, out, locale);
        break;
    case 'd':
        StoreStrftimePaddedNumber(value.tm_mday, 2, out, alternate);
        break;
    case 'j':
        StoreStrftimePaddedNumber(value.tm_yday + 1, 3, out, alternate);
        break;
    case 'm':
        StoreStrftimePaddedNumber(value.tm_mon + 1, 2, out, alternate);
        break;
    case 'p':
        append_bounded(out, value.tm_hour < 12 ? locale.am_name : locale.pm_name);
        break;
    case 'w':
        StoreStrftimePaddedNumber(value.tm_wday, 1, out, alternate);
        break;
    case 'x':
        FormatLocaleDateTimePattern(
            alternate ? locale.long_date_format.c_str() : locale.short_date_format.c_str(),
            value, out, locale);
        break;
    case 'y':
        StoreStrftimePaddedNumber(value.tm_year % 100, 2, out, alternate);
        break;
    default:
        break;
    }
}

void StoreStrftimePaddedNumber(int value, unsigned width, std::string& out,
    bool suppress_padding) {
    if (suppress_padding) {
        append_number_unpadded(value, out);
        return;
    }
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%0*d", static_cast<int>(width), value);
    out.append(buffer);
}

void FormatLocaleDateTimePattern(const char* pattern, const std::tm& value,
    std::string& out, const LocaleTimeData& locale) {
    if (pattern == nullptr) {
        return;
    }

    for (const char* cursor = pattern; *cursor != '\0';) {
        const char ch = *cursor;
        std::size_t run = 0;
        while (cursor[run] == ch) {
            ++run;
        }

        bool suppress_padding = false;
        char token = '\0';
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        switch (lower) {
        case '\'':
            ++cursor;
            while (*cursor != '\0' && *cursor != '\'') {
                out.push_back(*cursor++);
            }
            if (*cursor == '\'') {
                ++cursor;
            }
            continue;
        case 'a':
            if (_strnicmp(cursor, "am/pm", 5) == 0) {
                token = 'p';
                run = 5;
            } else if (_strnicmp(cursor, "a/p", 3) == 0) {
                token = 'p';
                run = 3;
            }
            break;
        case 'h':
            suppress_padding = run == 1;
            token = 'I';
            break;
        case 'm':
            if (run <= 2) {
                suppress_padding = run == 1;
                token = 'M';
            } else {
                token = run == 3 ? 'b' : 'B';
            }
            break;
        case 's':
            suppress_padding = run == 1;
            token = 'S';
            break;
        case 'd':
            if (run <= 2) {
                suppress_padding = run == 1;
                token = 'd';
            } else {
                token = run == 3 ? 'a' : 'A';
            }
            break;
        case 'y':
            token = run == 2 ? 'y' : 'Y';
            break;
        case 't': {
            const std::string& marker = value.tm_hour < 12 ? locale.am_name : locale.pm_name;
            out.append(marker.substr(0, std::min<std::size_t>(run, marker.size())));
            cursor += run;
            continue;
        }
        default:
            break;
        }

        if (token == '\0') {
            out.append(cursor, cursor + run);
        } else {
            FormatStrftimeToken(token, value, out, locale, suppress_padding);
        }
        cursor += run;
    }
}

std::tm* CrtLocalTime(const std::time_t* value) {
    if (value == nullptr || *value < 0) {
        return nullptr;
    }
    return std::localtime(value);
}

std::time_t CrtTime(std::time_t* out) {
    const std::time_t now = std::time(nullptr);
    if (out != nullptr) {
        *out = now;
    }
    return now;
}

void RunCrtExitTerminators() {
    for (;;) {
        LockCrtRuntime(0x0d);
        CrtOnExitFunction function = nullptr;
        if (!g_onexit_functions.empty()) {
            function = g_onexit_functions.back();
            g_onexit_functions.pop_back();
        }
        UnlockCrtRuntime(0x0d);
        if (function == nullptr) {
            break;
        }
        function();
    }
}

void CrtExit(int code) {
    CrtDoExit(code, false, false);
}

void CrtCeExit() {
    RunCrtExitTerminators();
}

void CrtCExit() {
    RunCrtExitTerminators();
}

void CrtDoExit(int code, bool quick_exit, bool cleanup_only) {
    if (!quick_exit) {
        RunCrtExitTerminators();
    }
    if (!cleanup_only) {
        std::exit(code);
    }
}

bool CrtCallAllocHook(int alloc_type, void* user_data, std::size_t size,
    int block_type, long request_number, const char* file_name,
    int line_number) {
    if (g_crt_alloc_hook == nullptr) {
        return true;
    }
    return g_crt_alloc_hook(alloc_type, user_data, size, block_type,
        request_number, file_name, line_number) != 0;
}

CrtAllocHookCallback CrtSetAllocHook(CrtAllocHookCallback callback) {
    CrtRuntimeLockScope lock(9);
    CrtAllocHookCallback previous = g_crt_alloc_hook;
    g_crt_alloc_hook = callback;
    return previous;
}

long CrtSetBreakAlloc(long allocation) {
    CrtRuntimeLockScope lock(9);
    const long previous = g_debug_break_allocation;
    g_debug_break_allocation = allocation;
    return previous;
}

void record_debug_allocation(void* memory, std::size_t size, int block_type,
    const char* file_name, int line_number) {
    if (memory == nullptr) {
        return;
    }
    CrtRuntimeLockScope lock(9);
    CrtDebugAllocation allocation{};
    allocation.size = size;
    allocation.block_type = block_type;
    allocation.file_name = file_name;
    allocation.line_number = line_number;
    allocation.sequence = ++g_debug_allocation_sequence;
    g_debug_allocations[memory] = allocation;
    g_debug_current_bytes += size;
    g_debug_total_allocated_bytes += size;
    g_debug_high_water_bytes =
        std::max(g_debug_high_water_bytes, g_debug_current_bytes);
}

void forget_debug_allocation(void* memory) {
    CrtRuntimeLockScope lock(9);
    auto it = g_debug_allocations.find(memory);
    if (it == g_debug_allocations.end()) {
        return;
    }
    g_debug_current_bytes =
        it->second.size > g_debug_current_bytes ? 0
            : g_debug_current_bytes - it->second.size;
    g_debug_allocations.erase(it);
}

void* CrtDebugHeapAllocTracked(std::size_t size, int block_type,
    const char* file_name, int line_number) {
    CrtRuntimeLockScope lock(9);
    const long request_number = static_cast<long>(g_debug_allocation_sequence + 1);
    if (request_number == g_debug_break_allocation) {
        CrtDebugBreak();
    }
    if (!CrtCallAllocHook(1, nullptr, size, block_type, request_number,
            file_name, line_number)) {
        return nullptr;
    }
    void* memory = std::malloc(size == 0 ? 1 : size);
    record_debug_allocation(memory, size, block_type, file_name, line_number);
    return memory;
}

void* CrtDebugHeapAlloc(std::size_t size) {
    return CrtDebugHeapAllocTracked(size, 1, nullptr, 0);
}

void* CrtMallocRetry(std::size_t size) {
    return CrtDebugHeapAlloc(size);
}

void* CrtReallocOrExpand(void* memory, std::size_t new_size, bool allow_move) {
    CrtRuntimeLockScope lock(9);
    if (memory == nullptr) {
        return CrtDebugHeapAlloc(new_size);
    }
    if (new_size == 0) {
        CrtDebugHeapFree(memory);
        return nullptr;
    }
    if (!allow_move) {
        const std::size_t current_size = CrtDebugMemorySize(memory);
        return new_size <= current_size ? memory : nullptr;
    }
    CrtDebugAllocation old_allocation{};
    const auto old = g_debug_allocations.find(memory);
    const bool was_tracked = old != g_debug_allocations.end();
    if (was_tracked) {
        old_allocation = old->second;
    }
    if (!CrtCallAllocHook(2, memory, new_size,
            was_tracked ? old_allocation.block_type : 1,
            was_tracked ? static_cast<long>(old_allocation.sequence) : 0,
            was_tracked ? old_allocation.file_name : nullptr,
            was_tracked ? old_allocation.line_number : 0)) {
        return nullptr;
    }
    void* result = std::realloc(memory, new_size == 0 ? 1 : new_size);
    if (result != nullptr) {
        if (was_tracked) {
            forget_debug_allocation(memory);
            record_debug_allocation(result, new_size, old_allocation.block_type,
                old_allocation.file_name, old_allocation.line_number);
        } else {
            record_debug_allocation(result, new_size, 1, nullptr, 0);
        }
    }
    return result;
}

void* CrtRealloc(void* memory, std::size_t new_size) {
    return CrtReallocOrExpand(memory, new_size, true);
}

void* CrtExpand(void* memory, std::size_t new_size) {
    return CrtReallocOrExpand(memory, new_size, false);
}

void CrtFree(void* memory) {
    CrtDebugHeapFree(memory);
}

void CrtDebugHeapFree(void* memory) {
    if (memory == nullptr) {
        return;
    }
    CrtRuntimeLockScope lock(9);
    const auto old = g_debug_allocations.find(memory);
    if (old != g_debug_allocations.end() &&
        !CrtCallAllocHook(3, memory, old->second.size, old->second.block_type,
            static_cast<long>(old->second.sequence), old->second.file_name,
            old->second.line_number)) {
        return;
    }
    forget_debug_allocation(memory);
    std::free(memory);
}

std::size_t CrtDebugMemorySize(void* memory) {
    if (memory == nullptr) {
        return 0;
    }
    CrtRuntimeLockScope lock(9);
    auto it = g_debug_allocations.find(memory);
    if (it != g_debug_allocations.end()) {
        return it->second.size;
    }
#ifdef _WIN32
    return _msize(memory);
#else
    return 0;
#endif
}

std::size_t CrtMemorySize(void* memory) {
    return CrtDebugMemorySize(memory);
}

void CrtSetDbgBlockType(void* memory, int block_type) {
    CrtRuntimeLockScope lock(9);
    auto it = g_debug_allocations.find(memory);
    if (it != g_debug_allocations.end()) {
        it->second.block_type = block_type;
    }
}

bool CrtCheckBytes(const void* memory, unsigned char expected, std::size_t size) {
    if (memory == nullptr && size != 0) {
        return false;
    }
    const auto* bytes = static_cast<const unsigned char*>(memory);
    for (std::size_t index = 0; index < size; ++index) {
        if (bytes[index] != expected) {
            return false;
        }
    }
    return true;
}

bool CrtCheckMemory() {
    CrtRuntimeLockScope lock(9);
    if ((g_crt_debug_flag & 1) == 0) {
        return true;
    }
#ifdef _WIN32
    if (HeapValidate(GetProcessHeap(), 0, nullptr) == 0) {
        return false;
    }
#endif
    for (const auto& [memory, allocation] : g_debug_allocations) {
        if (memory == nullptr) {
            return false;
        }
#ifdef _WIN32
        if (HeapValidate(GetProcessHeap(), 0, memory) == 0) {
            return false;
        }
#endif
        if (allocation.size > CrtDebugMemorySize(memory)) {
            return false;
        }
    }
    return true;
}

int CrtSetDebugFlag(int flag) {
    CrtRuntimeLockScope lock(9);
    const int previous = g_crt_debug_flag;
    if (flag != -1) {
        g_crt_debug_flag = flag;
    }
    return previous;
}

void CrtDoForAllClientObjects(CrtClientObjectCallback callback, void* context) {
    if (callback == nullptr) {
        return;
    }
    std::vector<void*> clients;
    {
        CrtRuntimeLockScope lock(9);
        if ((g_crt_debug_flag & 1) == 0) {
            return;
        }
        for (const auto& [memory, allocation] : g_debug_allocations) {
            if ((allocation.block_type & 0xffff) == 4) {
                clients.push_back(memory);
            }
        }
    }
    for (void* memory : clients) {
        callback(memory, context);
    }
}

bool CrtIsValidHeapPointer(void* memory) {
    if (memory == nullptr) {
        return false;
    }
    CrtRuntimeLockScope lock(9);
    if (g_debug_allocations.find(memory) != g_debug_allocations.end()) {
        return true;
    }
#ifdef _WIN32
    return HeapValidate(GetProcessHeap(), 0, memory) != 0;
#else
    return false;
#endif
}

void CrtMemCheckpoint(CrtMemState& state) {
    CrtRuntimeLockScope lock(9);
    state = {};
    state.first_block = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(g_debug_allocation_sequence));
    for (const auto& [memory, allocation] : g_debug_allocations) {
        (void)memory;
        const int block_type =
            allocation.block_type >= 0 && allocation.block_type < 5
                ? allocation.block_type
                : 1;
        state.block_count[block_type] += 1;
        state.block_bytes[block_type] += allocation.size;
    }
    state.high_water_bytes = g_debug_high_water_bytes;
    state.total_allocated_bytes = g_debug_total_allocated_bytes;
}

bool CrtMemDifference(CrtMemState& diff, const CrtMemState& old_state,
    const CrtMemState& new_state, bool include_crt_blocks) {
    diff = {};
    bool changed = false;
    for (std::size_t index = 0; index < 5; ++index) {
        diff.block_count[index] = new_state.block_count[index] - old_state.block_count[index];
        diff.block_bytes[index] = new_state.block_bytes[index] - old_state.block_bytes[index];
        if ((diff.block_count[index] != 0 || diff.block_bytes[index] != 0) &&
            index != 0 && (index != 2 || include_crt_blocks)) {
            changed = true;
        }
    }
    diff.high_water_bytes = new_state.high_water_bytes - old_state.high_water_bytes;
    diff.total_allocated_bytes =
        new_state.total_allocated_bytes - old_state.total_allocated_bytes;
    return changed;
}

void CrtDumpAllObjectsSince(const CrtMemState* state) {
    BreakIfCrtReportRequested(
        CrtDbgReport(0, nullptr, 0, nullptr, "Dumping objects ->\n"));
    unsigned long skipped = 0;
    if (state != nullptr && state->first_block != nullptr) {
        skipped = static_cast<unsigned long>(
            reinterpret_cast<std::uintptr_t>(state->first_block));
    }
    int debug_flag = 0;
    CrtDumpClientCallback dump_client = nullptr;
    std::vector<CrtDebugAllocationSnapshot> blocks;
    {
        CrtRuntimeLockScope lock(9);
        debug_flag = g_crt_debug_flag;
        dump_client = g_crt_dump_client;
        blocks.reserve(g_debug_allocations.size());
        for (const auto& [memory, allocation] : g_debug_allocations) {
            blocks.push_back({memory, allocation});
        }
    }

    for (const auto& block : blocks) {
        void* memory = block.memory;
        const CrtDebugAllocation& allocation = block.allocation;
        if (allocation.sequence <= skipped) {
            continue;
        }
        const int block_type = allocation.block_type & 0xffff;
        if (block_type == 0 || block_type == 3 ||
            (block_type == 2 && (debug_flag & 0x10) == 0)) {
            continue;
        }

        std::string prefix;
        if (allocation.file_name != nullptr) {
            char location[256]{};
            std::snprintf(location, sizeof(location), "%s(%d) : ",
                allocation.file_name, allocation.line_number);
            prefix = location;
        }

        char message[512]{};
        const unsigned subtype =
            static_cast<unsigned>(allocation.block_type >> 16) & 0xffffu;
        if (block_type == 4) {
            std::snprintf(message, sizeof(message),
                "%s{%lu} client block at %p, subtype %x, %zu bytes long.\n",
                prefix.c_str(), allocation.sequence, memory, subtype,
                allocation.size);
        } else if (block_type == 2) {
            std::snprintf(message, sizeof(message),
                "%s{%lu} crt block at %p, subtype %x, %zu bytes long.\n",
                prefix.c_str(), allocation.sequence, memory, subtype,
                allocation.size);
        } else {
            std::snprintf(message, sizeof(message),
                "%s{%lu} normal block at %p, %zu bytes long.\n",
                prefix.c_str(), allocation.sequence, memory, allocation.size);
        }
        BreakIfCrtReportRequested(
            CrtDbgReport(0, nullptr, 0, nullptr, "%s", message));

        if (block_type == 4 && dump_client != nullptr) {
            dump_client(memory, allocation.size);
        } else {
            CrtDumpBlockData(memory, allocation.size);
        }
    }
    BreakIfCrtReportRequested(
        CrtDbgReport(0, nullptr, 0, nullptr, "Object dump complete.\n"));
}

void CrtDumpBlockData(const void* memory, std::size_t size) {
    if (memory == nullptr || size == 0) {
        return;
    }
    const auto* bytes = static_cast<const unsigned char*>(memory);
    const std::size_t count = size < 16 ? size : 16;
    char text[17]{};
    char hex[16 * 3 + 1]{};
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char value = bytes[index];
        text[index] = static_cast<char>(value >= 0x20 && value < 0x7f
            ? value
            : ' ');
        std::snprintf(hex + index * 3, sizeof(hex) - index * 3,
            "%.2X ", value);
    }
    BreakIfCrtReportRequested(
        CrtDbgReport(0, nullptr, 0, nullptr, " Data: <%s> %s\n", text, hex));
}

bool CrtDumpMemoryLeaks() {
    CrtMemState state{};
    CrtMemCheckpoint(state);
    int debug_flag = 0;
    {
        CrtRuntimeLockScope lock(9);
        debug_flag = g_crt_debug_flag;
    }
    const bool leaked = state.block_count[1] != 0 || state.block_count[4] != 0 ||
        ((debug_flag & 0x10) != 0 && state.block_count[2] != 0);
    if (leaked) {
        BreakIfCrtReportRequested(
            CrtDbgReport(0, nullptr, 0, nullptr, "Detected memory leaks!\n"));
        CrtDumpAllObjectsSince(nullptr);
    }
    return leaked;
}

void CrtMemDumpStatistics(const CrtMemState& state) {
    static constexpr const char* kBlockNames[5] = {
        "Free", "Normal", "CRT", "Ignore", "Client"
    };
    for (std::size_t index = 0; index < 5; ++index) {
        BreakIfCrtReportRequested(CrtDbgReport(0, nullptr, 0, nullptr,
            "%zu bytes in %ld %s Blocks.\n", state.block_bytes[index],
            state.block_count[index], kBlockNames[index]));
    }
    BreakIfCrtReportRequested(CrtDbgReport(0, nullptr, 0, nullptr,
        "Largest number used: %zu bytes.\n", state.high_water_bytes));
    BreakIfCrtReportRequested(CrtDbgReport(0, nullptr, 0, nullptr,
        "Total allocations: %zu bytes.\n", state.total_allocated_bytes));
}

void* CrtMemMove(void* destination, const void* source, std::size_t size) {
    return std::memmove(destination, source, size);
}

void InitializeCrtFloatingPoint() {
    InitializeCrtFloatingPointTrapTable();
}

void InitializeCrtFloatingPointTrapTable() {
}

int SetCrtMathErrorMode(int mode) {
    const int previous = g_crt_math_error_mode;
    g_crt_math_error_mode = mode;
    return previous;
}

double CrtSin(double value) {
    return std::sin(value);
}

double CrtCos(double value) {
    return std::cos(value);
}

double CrtAtan(double value) {
    return std::atan(value);
}

double CrtFabs(double value) {
    return std::fabs(value);
}

double CrtFloor(double value) {
    return std::floor(value);
}

double CrtCeil(double value) {
    return std::ceil(value);
}

double CrtModf(double value, double* integer_part) {
    double local_integer = 0.0;
    double* out = integer_part != nullptr ? integer_part : &local_integer;
    return std::modf(value, out);
}

char* CrtGcvt(double value, int digits, char* destination) {
    if (destination == nullptr) {
        return nullptr;
    }
    if (digits <= 0) {
        digits = 1;
    }
    std::snprintf(destination, 64, "%.*g", digits, value);
    return destination;
}

void CrtPurecall() {
    CrtRuntimeErrorExitProcess(25);
}

bool LegacyFdivBugProbeFallback() {
    return false;
}

char* InsertLocaleDecimalPointBeforeExponent(char* text) {
    if (text == nullptr) {
        return nullptr;
    }
    char* insert_at = text;
    while (*insert_at != '\0' && *insert_at != 'e' && *insert_at != 'E') {
        ++insert_at;
    }
    std::memmove(insert_at + 1, insert_at, std::strlen(insert_at) + 1);
    *insert_at = locale_decimal_point_char();
    return text;
}

int FormatScientificFloat(double value, char* buffer, std::size_t buffer_chars,
    int precision, bool uppercase) {
    if (buffer == nullptr || buffer_chars == 0) {
        return -1;
    }
    return std::snprintf(buffer, buffer_chars, uppercase ? "%.*E" : "%.*e",
        precision, value);
}

char* BuildScientificFloatString(char* buffer, std::size_t buffer_chars,
    const char* digits, int exponent, bool negative, bool uppercase) {
    if (buffer == nullptr || buffer_chars == 0) {
        return buffer;
    }
    if (digits == nullptr || *digits == '\0') {
        digits = "0";
    }
    const char exponent_char = uppercase ? 'E' : 'e';
    const char sign = exponent < 0 ? '-' : '+';
    const int magnitude = exponent < 0 ? -exponent : exponent;
    if (digits[1] != '\0') {
        std::snprintf(buffer, buffer_chars, "%s%c%c%s%c%c%03d",
            negative ? "-" : "", digits[0], locale_decimal_point_char(),
            digits + 1, exponent_char, sign, magnitude);
    } else {
        std::snprintf(buffer, buffer_chars, "%s%c%c0%c%c%03d",
            negative ? "-" : "", digits[0], locale_decimal_point_char(),
            exponent_char, sign, magnitude);
    }
    return buffer;
}

int FormatFixedFloat(double value, char* buffer, std::size_t buffer_chars,
    int precision) {
    if (buffer == nullptr || buffer_chars == 0) {
        return -1;
    }
    return std::snprintf(buffer, buffer_chars, "%.*f", precision, value);
}

char* BuildFixedFloatString(char* buffer, std::size_t buffer_chars,
    const char* digits, int decimal_position, bool negative) {
    if (buffer == nullptr || buffer_chars == 0) {
        return buffer;
    }
    if (digits == nullptr || *digits == '\0') {
        digits = "0";
    }
    std::string out;
    if (negative) {
        out.push_back('-');
    }
    if (decimal_position <= 0) {
        out.push_back('0');
        out.push_back(locale_decimal_point_char());
        out.append(static_cast<std::size_t>(-decimal_position), '0');
        out.append(digits);
    } else {
        const auto digit_count = static_cast<int>(std::strlen(digits));
        if (digit_count <= decimal_position) {
            out.append(digits);
            out.append(static_cast<std::size_t>(decimal_position - digit_count), '0');
        } else {
            out.append(digits, static_cast<std::size_t>(decimal_position));
            out.push_back(locale_decimal_point_char());
            out.append(digits + decimal_position);
        }
    }
    std::snprintf(buffer, buffer_chars, "%s", out.c_str());
    return buffer;
}

int FormatGeneralFloat(double value, char* buffer, std::size_t buffer_chars,
    int precision, bool uppercase) {
    if (buffer == nullptr || buffer_chars == 0) {
        return -1;
    }
    return std::snprintf(buffer, buffer_chars, uppercase ? "%.*G" : "%.*g",
        precision, value);
}

int MapFpuStatusToMathError(unsigned status_word) {
    return (status_word & 0x80000u) != 0 ? 7 : 1;
}

unsigned ClassifyDoubleExponentBits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<unsigned>((bits >> 52) & 0x7ffu);
}

double StartTwoArgErrorHandling(double left, double, unsigned) {
    return left;
}

double HandleOneArgMathDomainError(const char*, double, double fallback,
    unsigned) {
    set_errno_from_math_type(1);
    return fallback;
}

double HandleTwoArgMathDomainError(const char*, double, double, double fallback,
    unsigned) {
    set_errno_from_math_type(1);
    return fallback;
}

double HandleOneArgFpuException(int exception_flags, const char*, double,
    double fallback, unsigned) {
    set_errno_from_math_type(math_type_from_exception_flags(exception_flags));
    return fallback;
}

double HandleTwoArgFpuException(int exception_flags, const char*, double,
    double, double fallback, unsigned) {
    set_errno_from_math_type(math_type_from_exception_flags(exception_flags));
    return fallback;
}

void BuildFpuExceptionRecord(CrtFpuExceptionState& state,
    unsigned exception_flags, unsigned control_word, unsigned status_word,
    double argument1, double argument2, double result) {
    state.exception_flags = exception_flags;
    state.control_word = control_word;
    state.status_word = status_word;
    state.argument1 = argument1;
    state.argument2 = argument2;
    state.result = result;
}

bool ApplyFpuExceptionMask(int exception_flags, double& value,
    unsigned control_word) {
    const int type = math_type_from_exception_flags(exception_flags);
    if (type != 0) {
        set_errno_from_math_type(type);
    }
    if ((exception_flags & 0x08) != 0 && (control_word & 0x08) != 0) {
        value = std::copysign(HUGE_VAL, value);
    } else if ((exception_flags & 0x04) != 0 && (control_word & 0x04) != 0) {
        value = 0.0;
    }
    return (exception_flags & ~control_word) == 0;
}

int MathErrorTypeFromExceptionFlags(unsigned flags) {
    if ((flags & 0x20) != 0) {
        return 5;
    }
    if ((flags & 0x08) != 0) {
        return 1;
    }
    if ((flags & 0x04) != 0) {
        return 2;
    }
    if ((flags & 0x01) != 0) {
        return 3;
    }
    if ((flags & 0x02) != 0) {
        return 4;
    }
    return 0;
}

double SetDoubleBiasedExponent(double value, int exponent) {
    if (value == 0.0 || !std::isfinite(value)) {
        return value;
    }
    int current_exponent = 0;
    const double mantissa = std::frexp(value, &current_exponent);
    return std::ldexp(mantissa, exponent + 1);
}

int GetDoubleUnbiasedExponent(double value) {
    if (value == 0.0 || !std::isfinite(value)) {
        return 0;
    }
    int exponent = 0;
    std::frexp(value, &exponent);
    return exponent - 1;
}

double ScaleDoubleByPowerOfTwo(double value, int exponent_delta) {
    return std::ldexp(value, exponent_delta);
}

double SetDoubleRawExponent(double value, unsigned raw_exponent) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    bits &= ~(0x7ffull << 52);
    bits |= (static_cast<std::uint64_t>(raw_exponent & 0x7ffu) << 52);
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

int ClassifyDoubleSpecial(double value) {
    if (std::isinf(value)) {
        return std::signbit(value) ? 2 : 1;
    }
    if (std::isnan(value)) {
        return 3;
    }
    return 0;
}

double NormalizeDoubleMantissa(double value, int* exponent) {
    int local_exponent = 0;
    const double mantissa = std::frexp(value, &local_exponent);
    if (exponent != nullptr) {
        *exponent = value == 0.0 ? 0 : local_exponent - 1;
    }
    return mantissa;
}

unsigned ReadFpuStatusWord() {
    return g_fpu_status_word;
}

unsigned ClearFpuStatusWord() {
    const unsigned previous = g_fpu_status_word;
    g_fpu_status_word = 0;
    return previous;
}

unsigned ReadFpuControlWord() {
    return g_fpu_control_word;
}

void RaiseFpuStatusFlags(unsigned flags) {
    g_fpu_status_word |= flags;
}

void CrtAbortWithThreadCallback() {
    std::abort();
}

void CrtAbortAfterThreadCallback() {
    CrtAbortWithThreadCallback();
}

void CrtTerminateAfterUnhandledException() {
    CrtAbortWithThreadCallback();
}

void AbortThunk() {
    CrtAbortWithThreadCallback();
}

void RestoreAbortExceptionFrame() {
}

int CompatMapString(unsigned locale_id, unsigned flags, const char* source,
    int source_chars, char* destination, int destination_chars,
    unsigned code_page, bool fail_invalid_chars) {
    if (source == nullptr) {
        return 0;
    }
    if (source_chars < 0) {
        source_chars = static_cast<int>(std::strlen(source)) + 1;
    } else {
        source_chars = CrtBoundedStringLength(source, source_chars);
    }
#ifdef _WIN32
    (void)code_page;
    (void)fail_invalid_chars;
    return LCMapStringA(static_cast<LCID>(locale_id), flags, source, source_chars,
        destination, destination_chars);
#else
    (void)locale_id;
    (void)flags;
    (void)code_page;
    (void)fail_invalid_chars;
    if (destination == nullptr || destination_chars == 0) {
        return source_chars;
    }
    const int count = source_chars < destination_chars ? source_chars : destination_chars;
    std::memcpy(destination, source, static_cast<std::size_t>(count));
    return count;
#endif
}

int CrtBoundedStringLength(const char* text, int max_chars) {
    if (text == nullptr || max_chars <= 0) {
        return 0;
    }
    int count = 0;
    while (count < max_chars && text[count] != '\0') {
        ++count;
    }
    return count;
}

int CrtFillStreamBuffer(FILE* stream) {
    return stream != nullptr ? std::fgetc(stream) : EOF;
}

int CrtReadFileDescriptorLocked(int file_descriptor, void* buffer,
    unsigned bytes) {
    return CrtReadFileDescriptorNoLock(file_descriptor, buffer, bytes);
}

int CrtReadFileDescriptorNoLock(int file_descriptor, void* buffer,
    unsigned bytes) {
    if (buffer == nullptr && bytes != 0) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
#ifdef _WIN32
    return _read(file_descriptor, buffer, bytes);
#else
    return static_cast<int>(std::fread(buffer, 1, bytes, stdin));
#endif
}

int AllocateCrtFileDescriptor() {
    static int next_descriptor = 3;
    return next_descriptor++;
}

int ClearCrtFileDescriptorHandle(int file_descriptor) {
    return file_descriptor >= 0 ? 0 : -1;
}

intptr_t CrtGetOsFileHandle(int file_descriptor) {
#ifdef _WIN32
    return _get_osfhandle(file_descriptor);
#else
    return file_descriptor;
#endif
}

int CrtOpenOsFileHandle(void* os_handle, unsigned flags) {
#ifdef _WIN32
    return _open_osfhandle(reinterpret_cast<intptr_t>(os_handle),
        static_cast<int>(flags));
#else
    (void)flags;
    return os_handle != nullptr ? AllocateCrtFileDescriptor() : -1;
#endif
}

void LockCrtFileDescriptor(int) {
}

int CrtSeekFileDescriptorLocked(int file_descriptor, long offset, int origin) {
    return static_cast<int>(CrtSeekFileDescriptorNoLock(file_descriptor, offset,
        origin));
}

long CrtSeekFileDescriptorNoLock(int file_descriptor, long offset, int origin) {
#ifdef _WIN32
    return _lseek(file_descriptor, offset, origin);
#else
    (void)file_descriptor;
    (void)origin;
    return offset;
#endif
}

void InitializeCrtIoTable() {
}

int CrtWriteFileDescriptorLocked(int file_descriptor, const void* buffer,
    unsigned bytes) {
    return CrtWriteFileDescriptorNoLock(file_descriptor, buffer, bytes);
}

int CrtWriteFileDescriptorNoLock(int file_descriptor, const void* buffer,
    unsigned bytes) {
    if (buffer == nullptr && bytes != 0) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
#ifdef _WIN32
    return _write(file_descriptor, buffer, bytes);
#else
    return static_cast<int>(std::fwrite(buffer, 1, bytes, stdout));
#endif
}

std::string CrtSetLocaleCategory(int category, const char* locale_name) {
    const int mapped = map_locale_category(category);
    if (mapped < 0) {
        return {};
    }
    const char* result = std::setlocale(mapped, locale_name);
    return result != nullptr ? std::string(result) : std::string();
}

bool CrtApplyLocaleCategory(int category, const char* locale_name) {
    return !CrtSetLocaleCategory(category, locale_name).empty();
}

std::string BuildCompositeLocaleString() {
    const char* locale = std::setlocale(LC_ALL, nullptr);
    return locale != nullptr ? std::string(locale) : std::string("C");
}

std::string ResolveLocaleName(const char* locale_name, CrtLocaleNameParts* parts) {
    if (parts != nullptr) {
        *parts = {};
    }
    if (locale_name == nullptr) {
        return {};
    }
    if (std::strcmp(locale_name, "C") == 0 || *locale_name == '\0') {
        if (parts != nullptr) {
            parts->language = "C";
        }
        return "C";
    }
    CrtLocaleNameParts parsed;
    if (!ParseLocaleNameParts(locale_name, parsed)) {
        return {};
    }
    if (parts != nullptr) {
        *parts = parsed;
    }
    return std::string(locale_name);
}

int CrtLocaleNoOp() {
    return 0;
}

bool ParseLocaleNameParts(const char* locale_name, CrtLocaleNameParts& parts) {
    parts = {};
    if (locale_name == nullptr) {
        return false;
    }
    std::string value(locale_name);
    const std::size_t dot = value.find('.');
    if (dot != std::string::npos) {
        parts.code_page = value.substr(dot + 1);
        value.erase(dot);
    }
    const std::size_t sep = value.find_first_of("_-");
    if (sep == std::string::npos) {
        parts.language = value;
    } else {
        parts.language = value.substr(0, sep);
        parts.country = value.substr(sep + 1);
    }
    return !parts.language.empty();
}

int CxxFrameHandler(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    int catch_depth, void* nested_registration, bool unwind_target) {
    if (function_info == nullptr) {
        return 1;
    }
    DispatchCxxException(exception_record, registration, context,
        dispatcher_context, function_info, false, catch_depth,
        nested_registration);
    if (unwind_target) {
        UnwindCxxFrameToState(registration, dispatcher_context, function_info, -1);
    }
    return 1;
}

void DispatchCxxException(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    bool destruct_exception_object, int catch_depth, void* nested_registration) {
    if (exception_record == nullptr && destruct_exception_object) {
        CrtAbortWithThreadCallback();
    }
    (void)context;
    (void)dispatcher_context;
    (void)function_info;
    (void)catch_depth;
    (void)nested_registration;
    (void)registration;
}

void TranslateAndDispatchSehException(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    int current_state, int catch_depth, void* nested_registration) {
    (void)current_state;
    DispatchCxxException(exception_record, registration, context,
        dispatcher_context, function_info, false, catch_depth,
        nested_registration);
}

bool CxxCatchTypeMatches(const char* handler_type, const char* thrown_type,
    unsigned handler_attributes, unsigned thrown_attributes) {
    if (handler_type == nullptr || *handler_type == '\0') {
        return true;
    }
    if (thrown_type == nullptr) {
        return false;
    }
    if (std::strcmp(handler_type, thrown_type) != 0) {
        return false;
    }
    if ((thrown_attributes & 0x01u) != 0 && (handler_attributes & 0x01u) == 0) {
        return false;
    }
    if ((thrown_attributes & 0x02u) != 0 && (handler_attributes & 0x02u) == 0) {
        return false;
    }
    return true;
}

void UnwindCxxFrameToState(void*, void*, void*, int) {
}

int CallCxxCatchHandler(void*, void*, void*, void*, void*, int, unsigned) {
    return 0;
}

void RestoreCatchContextAndDestruct(void*, bool) {
}

int FinishCatchHandlerCall() {
    return 0;
}

bool IsPureCxxRethrow(const void* exception_record) {
    return exception_record == nullptr;
}

void CopyExceptionObjectToCatch(void* destination, const void* source,
    std::size_t size) {
    if (destination != nullptr && source != nullptr && size != 0) {
        std::memmove(destination, source, size);
    }
}

char* CrtFindMbcsStringOneOf(char* text, const char* characters) {
    return FindMbcsStringOneOf(text, characters);
}

int AsciiUpperToLower(int character) {
    return character + 0x20;
}

int CrtToLower(int character) {
    if (character >= 'A' && character <= 'Z') {
        return AsciiUpperToLower(character);
    }
    return character;
}

int CrtToLowerLocale(int character) {
    if (character < 0 || character > 0xff) {
        return character;
    }
    return std::tolower(static_cast<unsigned char>(character));
}

long long CrtFtell64(FILE* stream) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
    LockCrtStream(stream);
    const long long result = CrtFtell64Unlocked(stream);
    UnlockCrtStream(stream);
    return result;
}

long long CrtFtell64Unlocked(FILE* stream) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
#ifdef _WIN32
    return _ftelli64(stream);
#else
    return std::ftell(stream);
#endif
}

CrtSignalHandler CrtSignal(int signal, CrtSignalHandler handler) {
    if (signal < 0 || signal >= static_cast<int>(g_signal_handlers.size())) {
        *CrtErrnoPointer() = EINVAL;
        return reinterpret_cast<CrtSignalHandler>(-1);
    }
    CrtSignalHandler previous = g_signal_handlers[static_cast<std::size_t>(signal)];
    g_signal_handlers[static_cast<std::size_t>(signal)] = handler;
    return previous;
}

bool CrtConsoleCtrlSignalHandler(unsigned control_type) {
    const int signal = control_type == 0 ? 2 : 0x15;
    return CrtRaiseSignal(signal) == 0;
}

int CrtRaiseSignal(int signal) {
    CrtSignalHandler* slot = FindSignalAction(signal);
    if (slot == nullptr) {
        return -1;
    }
    CrtSignalHandler handler = *slot;
    if (handler == nullptr) {
        return -1;
    }
    if (handler == reinterpret_cast<CrtSignalHandler>(1)) {
        return 0;
    }
    *slot = nullptr;
    handler(signal);
    return 0;
}

CrtSignalHandler* FindSignalAction(int signal) {
    if (signal < 0 || signal >= static_cast<int>(g_signal_handlers.size())) {
        return nullptr;
    }
    return &g_signal_handlers[static_cast<std::size_t>(signal)];
}

int* CrtFpecodePointer() {
    return &g_floating_point_exception_code;
}

void** CrtExceptionPointerSlot() {
    return &g_exception_pointer;
}

int CrtMessageBox(const char* text, const char* title, unsigned flags) {
#ifdef _WIN32
    HWND owner = GetActiveWindow();
    if (owner != nullptr) {
        owner = GetLastActivePopup(owner);
    }
    return MessageBoxA(owner, text != nullptr ? text : "",
        title != nullptr ? title : "", flags);
#else
    std::fprintf(stderr, "%s: %s\n", title != nullptr ? title : "message",
        text != nullptr ? text : "");
    return 0;
#endif
}

int CrtSetStreamBuffering(FILE* stream, char* buffer, unsigned mode,
    unsigned size) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }

    int mapped_mode = _IOFBF;
    std::size_t mapped_size = size & ~1u;
    switch (mode) {
    case 0:
        mapped_mode = _IOFBF;
        break;
    case 0x40:
        mapped_mode = _IOLBF;
        break;
    case 4:
        mapped_mode = _IONBF;
        mapped_size = 0;
        break;
    default:
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }

    if (mode != 4 && mapped_size < 2) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
    LockCrtStream(stream);
    const int result = std::setvbuf(stream, buffer, mapped_mode, mapped_size);
    UnlockCrtStream(stream);
    return result;
}

int CrtOpenFileDescriptor(const char* path, unsigned open_flags,
    unsigned share_flags, unsigned permission) {
    (void)share_flags;
    if (path == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        *CrtDosErrnoPointer() = 0;
        return -1;
    }
#ifdef _WIN32
    const int result = _open(path, static_cast<int>(open_flags),
        static_cast<int>(permission));
    if (result < 0) {
        *CrtDosErrnoPointer() = GetLastError();
    }
    return result;
#else
    (void)open_flags;
    (void)permission;
    *CrtErrnoPointer() = ENOSYS;
    return -1;
#endif
}

CrtExceptionSignalAction* FindExceptionSignalAction(unsigned exception_code,
    CrtExceptionSignalAction* actions, std::size_t action_count) {
    for (std::size_t index = 0; index < action_count; ++index) {
        if (actions[index].exception_code == exception_code) {
            return &actions[index];
        }
    }
    return nullptr;
}

const char* GetCommandLineArgumentsStart(const char* command_line) {
#ifdef _WIN32
    if (command_line == nullptr) {
        command_line = GetCommandLineA();
    }
#endif
    if (command_line == nullptr) {
        return "";
    }

    const char* cursor = command_line;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor != '\0' && *cursor != '"') {
            cursor += command_line_char_bytes(cursor);
        }
        if (*cursor == '"') {
            ++cursor;
        }
    } else {
        while (*cursor != '\0' && !is_command_space(*cursor)) {
            cursor += command_line_char_bytes(cursor);
        }
    }
    while (is_command_space(*cursor)) {
        ++cursor;
    }
    return cursor;
}

std::vector<std::string> InitializeEnvironmentVector(
    const char* environment_block) {
    std::string owned_block;
    if (environment_block == nullptr) {
        owned_block = CloneAnsiEnvironmentBlock();
        environment_block = owned_block.c_str();
    }

    std::vector<std::string> environment;
    if (environment_block == nullptr) {
        return environment;
    }
    for (const char* cursor = environment_block; *cursor != '\0';
         cursor += std::strlen(cursor) + 1) {
        if (*cursor != '=') {
            environment.emplace_back(cursor);
        }
    }
    return environment;
}

std::vector<std::string> InitializeArgumentVector(const char* command_line) {
    std::vector<std::string> arguments;
    ParseCommandLineArguments(command_line, arguments);
    return arguments;
}

void ParseCommandLineArguments(const char* command_line,
    std::vector<std::string>& arguments) {
#ifdef _WIN32
    if (command_line == nullptr) {
        command_line = GetCommandLineA();
    }
#endif
    arguments.clear();
    if (command_line == nullptr) {
        return;
    }

    const char* cursor = command_line;
    std::string program_name;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor != '\0' && *cursor != '"') {
            append_command_line_char(program_name, cursor);
        }
        if (*cursor == '"') {
            ++cursor;
        }
    } else {
        while (*cursor != '\0' && !is_command_space(*cursor)) {
            append_command_line_char(program_name, cursor);
        }
    }
    arguments.push_back(program_name);

    while (is_command_space(*cursor)) {
        ++cursor;
    }
    while (*cursor != '\0') {
        std::string argument;
        bool in_quotes = false;
        while (*cursor != '\0') {
            if (!in_quotes && is_command_space(*cursor)) {
                break;
            }

            std::size_t slash_count = 0;
            while (*cursor == '\\') {
                ++slash_count;
                ++cursor;
            }

            if (*cursor == '"') {
                argument.append(slash_count / 2, '\\');
                if ((slash_count & 1u) != 0) {
                    argument.push_back('"');
                } else if (in_quotes && cursor[1] == '"') {
                    argument.push_back('"');
                    ++cursor;
                } else {
                    in_quotes = !in_quotes;
                }
                ++cursor;
                continue;
            }

            argument.append(slash_count, '\\');
            if (*cursor == '\0' || (!in_quotes && is_command_space(*cursor))) {
                break;
            }
            append_command_line_char(argument, cursor);
        }
        arguments.push_back(argument);
        while (is_command_space(*cursor)) {
            ++cursor;
        }
    }
}

std::string CloneAnsiEnvironmentBlock() {
#ifdef _WIN32
    if (LPWCH wide_block = GetEnvironmentStringsW()) {
        const std::size_t wide_chars = wide_environment_block_length(wide_block);
        const int bytes = WideCharToMultiByte(CP_ACP, 0, wide_block,
            static_cast<int>(wide_chars), nullptr, 0, nullptr, nullptr);
        std::string result(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
        if (bytes > 0) {
            WideCharToMultiByte(CP_ACP, 0, wide_block,
                static_cast<int>(wide_chars), result.data(), bytes, nullptr,
                nullptr);
        }
        FreeEnvironmentStringsW(wide_block);
        return result;
    }
    if (LPCH ansi_block = GetEnvironmentStringsA()) {
        const std::size_t bytes = ansi_environment_block_length(ansi_block);
        std::string result(ansi_block, ansi_block + bytes);
        FreeEnvironmentStringsA(ansi_block);
        return result;
    }
#endif
    return std::string(1, '\0');
}

void CrtPrintRuntimeMessageBanner(int message_id) {
    const char* message = CrtGetRuntimeErrorMessage(message_id);
    if (message == nullptr) {
        return;
    }
#ifdef _WIN32
    DWORD written = 0;
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error != INVALID_HANDLE_VALUE && error != nullptr) {
        WriteFile(error, message, static_cast<DWORD>(std::strlen(message)),
            &written, nullptr);
        return;
    }
#endif
    std::fwrite(message, 1, std::strlen(message), stderr);
}

const char* CrtGetRuntimeErrorMessage(int message_id) {
    for (const RuntimeErrorMessage& message : kRuntimeErrorMessages) {
        if (message.id == message_id) {
            return message.text;
        }
    }
    return nullptr;
}

unsigned CrtToLowerMbcs(unsigned character) {
    if (character < 0x100) {
        return static_cast<unsigned>(
            std::tolower(static_cast<unsigned char>(character)));
    }
#ifdef _WIN32
    char source[2] = {
        static_cast<char>((character >> 8) & 0xffu),
        static_cast<char>(character & 0xffu),
    };
    char mapped[2] = {};
    if (LCMapStringA(LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, source, 2,
        mapped, 2) != 0) {
        return (static_cast<unsigned char>(mapped[0]) << 8) |
            static_cast<unsigned char>(mapped[1]);
    }
#endif
    return character;
}

#ifdef _WIN32
LONG WINAPI CrtUnhandledExceptionFilterThunk(
    EXCEPTION_POINTERS* exception_pointers) {
    if (exception_pointers != nullptr &&
        exception_pointers->ExceptionRecord != nullptr) {
        const EXCEPTION_RECORD& record = *exception_pointers->ExceptionRecord;
        if (record.ExceptionCode == 0xe06d7363u &&
            record.NumberParameters >= 1 &&
            record.ExceptionInformation[0] == 0x19930520u) {
            CrtAbortWithThreadCallback();
        }
    }
    if (g_previous_unhandled_exception_filter != nullptr) {
        return g_previous_unhandled_exception_filter(exception_pointers);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
int CrtUnhandledExceptionFilterThunk(void*) {
    return 0;
}
#endif

void InstallCrtUnhandledExceptionFilter() {
#ifdef _WIN32
    g_previous_unhandled_exception_filter =
        SetUnhandledExceptionFilter(CrtUnhandledExceptionFilterThunk);
#endif
}

void RestoreCrtUnhandledExceptionFilter() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(g_previous_unhandled_exception_filter);
#endif
}

int CrtWideOutputFormatCore(CrtWideOutputWriter writer, void* context,
    const wchar_t* format, va_list args) {
    if (writer == nullptr || format == nullptr) {
        return -1;
    }

    std::vector<wchar_t> buffer(512);
    int written = -1;
    for (;;) {
        va_list copy;
        va_copy(copy, args);
        written = std::vswprintf(buffer.data(), buffer.size(), format, copy);
        va_end(copy);
        if (written >= 0 &&
            static_cast<std::size_t>(written) < buffer.size()) {
            break;
        }
        if (written >= 0) {
            buffer.resize(static_cast<std::size_t>(written) + 1);
        } else {
            if (buffer.size() >= 1u << 20) {
                return -1;
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    int output_count = 0;
    return CrtWriteWideOutputSpan(buffer.data(), written, writer, context,
        output_count);
}

int CrtWriteWideOutputChar(wchar_t character, CrtWideOutputWriter writer,
    void* context, int& count) {
    if (count < 0 || writer == nullptr) {
        count = -1;
        return -1;
    }
    if (writer(character, context) < 0) {
        count = -1;
        return -1;
    }
    ++count;
    return count;
}

int CrtWriteWideOutputSpan(const wchar_t* text, int count,
    CrtWideOutputWriter writer, void* context, int& written_count) {
    if (text == nullptr || count < 0) {
        written_count = -1;
        return -1;
    }
    for (int index = 0; index < count; ++index) {
        if (CrtWriteWideOutputChar(text[index], writer, context,
            written_count) < 0) {
            return -1;
        }
    }
    return written_count;
}

std::uint32_t CrtReadOutputArgument32(const unsigned char*& cursor) {
    return CrtReadVaArg32(cursor);
}

std::uint64_t CrtReadOutputArgument64(const unsigned char*& cursor) {
    return CrtReadVaArg64(cursor);
}

int CrtCompareStringCompat(unsigned locale_id, unsigned flags, const char* left,
    int left_chars, const char* right, int right_chars, unsigned code_page) {
    (void)code_page;
    if (left == nullptr || right == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return 0;
    }
    if (left_chars > 0) {
        left_chars = CrtAnsiStringLengthBounded(left, left_chars);
    }
    if (right_chars > 0) {
        right_chars = CrtAnsiStringLengthBounded(right, right_chars);
    }
#ifdef _WIN32
    const int result = CompareStringA(locale_id != 0 ? locale_id : LOCALE_USER_DEFAULT,
        flags, left, left_chars, right, right_chars);
    if (result != 0) {
        return result;
    }
#endif
    const int compare = std::strcmp(left, right);
    return compare < 0 ? 1 : (compare > 0 ? 3 : 2);
}

int CrtAnsiStringLengthBounded(const char* text, int max_chars) {
    return CrtBoundedStringLength(text, max_chars);
}

char* CrtFindCharInSet(char* text, const char* characters) {
    return const_cast<char*>(CrtFindCharInSet(
        static_cast<const char*>(text), characters));
}

const char* CrtFindCharInSet(const char* text, const char* characters) {
    if (text == nullptr || characters == nullptr) {
        return nullptr;
    }
    return std::strpbrk(text, characters);
}

bool CrtGetStringTypeCompat(unsigned info_type, const char* text, int chars,
    unsigned short* types, unsigned code_page, unsigned locale_id,
    bool fail_invalid) {
    if (text == nullptr || types == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return false;
    }
#ifdef _WIN32
    if (locale_id != 0 &&
        GetStringTypeA(static_cast<LCID>(locale_id), static_cast<DWORD>(info_type),
            text, chars, reinterpret_cast<LPWORD>(types)) != FALSE) {
        return true;
    }
    const UINT mapped_code_page = code_page != 0 ? code_page : CP_ACP;
    const DWORD flags = fail_invalid ? MB_ERR_INVALID_CHARS : 0;
    const int wide_chars = MultiByteToWideChar(mapped_code_page, flags, text,
        chars, nullptr, 0);
    if (wide_chars > 0) {
        std::vector<wchar_t> wide(static_cast<std::size_t>(wide_chars));
        if (MultiByteToWideChar(mapped_code_page, flags, text, chars,
            wide.data(), wide_chars) > 0 &&
            GetStringTypeW(static_cast<DWORD>(info_type), wide.data(),
                wide_chars, reinterpret_cast<LPWORD>(types)) != FALSE) {
            return true;
        }
    }
#else
    (void)info_type;
    (void)chars;
    (void)code_page;
    (void)locale_id;
    (void)fail_invalid;
#endif
    for (int index = 0; index < chars && text[index] != '\0'; ++index) {
        types[index] = static_cast<unsigned short>(
            CrtIsCharType(static_cast<unsigned char>(text[index]), 0xffff));
    }
    return true;
}

double CrtRoundToNearest(double value) {
    return std::nearbyint(value);
}

void CrtFloatConversionThunk(void* destination, const void* source, int mode) {
    (void)destination;
    (void)source;
    (void)mode;
}

double CrtLogb(double value) {
    return std::logb(value);
}

double CrtNextAfter(double value, double target) {
    return std::nextafter(value, target);
}

bool CrtIsFiniteDouble(double value) {
    return std::isfinite(value);
}

bool CrtIsNanDouble(double value) {
    return std::isnan(value);
}

int CrtFpClassifyDouble(double value) {
    const bool negative = std::signbit(value);
    if (std::isnan(value)) {
        return 0x0002;
    }
    if (std::isinf(value)) {
        return negative ? 0x0004 : 0x0200;
    }
    if (value == 0.0) {
        return negative ? 0x0020 : 0x0040;
    }
    if (std::fpclassify(value) == FP_SUBNORMAL) {
        return negative ? 0x0010 : 0x0080;
    }
    return negative ? 0x0008 : 0x0100;
}

std::size_t CrtStringSpanIncluding(const char* text, const char* characters) {
    if (text == nullptr || characters == nullptr) {
        return 0;
    }
    return std::strspn(text, characters);
}

std::size_t CrtStringSpanExcluding(const char* text, const char* characters) {
    if (text == nullptr || characters == nullptr) {
        return 0;
    }
    return std::strcspn(text, characters);
}

CrtDecimalConversion ConvertDoubleToDecimalString(double value, int precision) {
    if (precision <= 0) {
        precision = 17;
    }
    CrtDecimalConversion result;
    result.sign = std::signbit(value) ? 1 : 0;
    const double magnitude = std::fabs(value);
    if (!std::isfinite(magnitude)) {
        result.status = std::isnan(magnitude) ? 1 : 2;
        result.digits = std::isnan(magnitude) ? "1#QNAN" : "1#INF";
        return result;
    }

    char format[16]{};
    std::snprintf(format, sizeof(format), "%%.%de", precision - 1);
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), format, magnitude);
    std::string text(buffer);
    const std::size_t exponent_pos = text.find('e');
    int exponent = 0;
    if (exponent_pos != std::string::npos) {
        exponent = std::atoi(text.c_str() + exponent_pos + 1);
        text.erase(exponent_pos);
    }
    text.erase(std::remove(text.begin(), text.end(), '.'), text.end());
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    result.decimal_point = exponent + 1;
    result.digits = text;
    return result;
}

CrtLongDouble80 ConvertDoubleToLongDoubleBits(double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    const std::uint16_t sign = static_cast<std::uint16_t>((raw >> 63) << 15);
    std::uint16_t exponent = static_cast<std::uint16_t>((raw >> 52) & 0x7ffu);
    std::uint64_t mantissa = raw & ((std::uint64_t{1} << 52) - 1);

    CrtLongDouble80 out;
    if (exponent == 0 && mantissa == 0) {
        out.sign_exponent = sign;
        return out;
    }
    if (exponent == 0x7ffu) {
        out.sign_exponent = sign | 0x7fffu;
        mantissa = (std::uint64_t{1} << 63) | (mantissa << 11);
    } else {
        if (exponent == 0) {
            exponent = 1;
            while ((mantissa & (std::uint64_t{1} << 52)) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= ((std::uint64_t{1} << 52) - 1);
        }
        out.sign_exponent = sign |
            static_cast<std::uint16_t>(exponent + 0x3c00u);
        mantissa = (std::uint64_t{1} << 63) | (mantissa << 11);
    }
    out.mantissa_low = static_cast<std::uint32_t>(mantissa);
    out.mantissa_high = static_cast<std::uint32_t>(mantissa >> 32);
    return out;
}

bool CrtGetStreamBuffer(FILE* stream, std::vector<char>& storage) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return false;
    }
    storage.assign(0x1000, '\0');
    if (std::setvbuf(stream, storage.data(), _IOFBF, storage.size()) != 0) {
        storage.assign(2, '\0');
        return std::setvbuf(stream, storage.data(), _IONBF, 0) == 0;
    }
    return true;
}

int CrtWideCharToMultibyteLocked(char* destination, wchar_t character,
    unsigned code_page, int destination_chars, bool fail_invalid) {
    LockCrtRuntime(0x13);
    const int result = CrtWideCharToMultibyteNoLock(destination, character,
        code_page, destination_chars, fail_invalid);
    UnlockCrtRuntime(0x13);
    return result;
}

int CrtWideCharToMultibyteNoLock(char* destination, wchar_t character,
    unsigned code_page, int destination_chars, bool fail_invalid) {
    if (destination == nullptr || destination_chars <= 0) {
        return 0;
    }
#ifdef _WIN32
    BOOL used_default = FALSE;
    const UINT mapped_code_page = code_page != 0 ? code_page : CP_ACP;
    const DWORD flags = fail_invalid ? WC_NO_BEST_FIT_CHARS : 0;
    const int result = WideCharToMultiByte(mapped_code_page, flags, &character,
        1, destination, destination_chars, nullptr,
        fail_invalid ? &used_default : nullptr);
    if (result == 0 || used_default != FALSE) {
        *CrtErrnoPointer() = EILSEQ;
        return -1;
    }
    return result;
#else
    (void)code_page;
    (void)fail_invalid;
    std::mbstate_t state{};
    const int result = static_cast<int>(std::wcrtomb(destination, character,
        &state));
    if (result < 0 || result > destination_chars) {
        *CrtErrnoPointer() = EILSEQ;
        return -1;
    }
    return result;
#endif
}

int CrtCloseAllStreams() {
    return CrtFlushAllStreams(true);
}

int CrtFlushFileDescriptor(int file_descriptor) {
#ifdef _WIN32
    if (_commit(file_descriptor) != 0) {
        *CrtDosErrnoPointer() = GetLastError();
        return -1;
    }
    return 0;
#else
    (void)file_descriptor;
    return 0;
#endif
}

char* CrtGetenvLocked(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    LockCrtRuntime(0x0c);
    char* value = std::getenv(name);
    UnlockCrtRuntime(0x0c);
    return value;
}

unsigned ReadFpuControlWordMapped() {
    return MapX87ControlWordToCrt(ReadFpuControlWord());
}

unsigned ReadSseControlWordMapped() {
    return MapSseControlWordToCrt(ReadFpuControlWord());
}

unsigned CrtControlFpMasked(unsigned new_value, unsigned mask) {
    const unsigned current = ReadFpuControlWordMapped();
    const unsigned updated = (new_value & mask) | (current & ~mask);
    g_fpu_control_word = MapCrtControlWordToX87(updated);
    return updated;
}

void CrtSetDefaultFpuPrecision() {
    (void)CrtControlFpMasked(0x00010000u, 0x00030000u);
}

unsigned MapX87ControlWordToCrt(unsigned control_word) {
    unsigned out = 0;
    if ((control_word & 0x0001u) != 0) {
        out |= 0x00000010u;
    }
    if ((control_word & 0x0004u) != 0) {
        out |= 0x00000008u;
    }
    if ((control_word & 0x0008u) != 0) {
        out |= 0x00000004u;
    }
    if ((control_word & 0x0010u) != 0) {
        out |= 0x00000002u;
    }
    if ((control_word & 0x0020u) != 0) {
        out |= 0x00000001u;
    }
    if ((control_word & 0x0002u) != 0) {
        out |= 0x00080000u;
    }
    switch (control_word & 0x0c00u) {
    case 0x0400:
        out |= 0x00000100u;
        break;
    case 0x0800:
        out |= 0x00000200u;
        break;
    case 0x0c00:
        out |= 0x00000300u;
        break;
    }
    switch (control_word & 0x0300u) {
    case 0x0000:
        out |= 0x00020000u;
        break;
    case 0x0200:
        out |= 0x00010000u;
        break;
    }
    if ((control_word & 0x1000u) != 0) {
        out |= 0x00040000u;
    }
    return out;
}

unsigned MapCrtControlWordToX87(unsigned control_word) {
    unsigned out = 0;
    if ((control_word & 0x00000010u) != 0) {
        out |= 0x0001u;
    }
    if ((control_word & 0x00000008u) != 0) {
        out |= 0x0004u;
    }
    if ((control_word & 0x00000004u) != 0) {
        out |= 0x0008u;
    }
    if ((control_word & 0x00000002u) != 0) {
        out |= 0x0010u;
    }
    if ((control_word & 0x00000001u) != 0) {
        out |= 0x0020u;
    }
    if ((control_word & 0x00080000u) != 0) {
        out |= 0x0002u;
    }
    switch (control_word & 0x00000300u) {
    case 0x00000100:
        out |= 0x0400u;
        break;
    case 0x00000200:
        out |= 0x0800u;
        break;
    case 0x00000300:
        out |= 0x0c00u;
        break;
    }
    switch (control_word & 0x00030000u) {
    case 0x00000000:
        out |= 0x0300u;
        break;
    case 0x00010000:
        out |= 0x0200u;
        break;
    }
    if ((control_word & 0x00040000u) != 0) {
        out |= 0x1000u;
    }
    return out;
}

unsigned MapSseControlWordToCrt(unsigned control_word) {
    return MapX87ControlWordToCrt(control_word);
}

bool MantissaHasNoBitsAfter(const CrtMantissa96& mantissa, int bit_index) {
    if (bit_index < 0) {
        return false;
    }
    for (int bit = bit_index + 1; bit < 96; ++bit) {
        const int word = bit / 32;
        const int shift = 31 - (bit & 31);
        if ((mantissa.words[static_cast<std::size_t>(word)] &
            (std::uint32_t{1} << shift)) != 0) {
            return false;
        }
    }
    return true;
}

bool RoundMantissaToBit(CrtMantissa96& mantissa, int bit_index) {
    if (bit_index <= 0 || bit_index >= 96) {
        return false;
    }
    const int word = bit_index / 32;
    const int shift = 31 - (bit_index & 31);
    const bool round_bit = (mantissa.words[static_cast<std::size_t>(word)] &
        (std::uint32_t{1} << shift)) != 0;
    const bool sticky = !MantissaHasNoBitsAfter(mantissa, bit_index);
    for (int bit = bit_index; bit < 96; ++bit) {
        const int clear_word = bit / 32;
        const int clear_shift = 31 - (bit & 31);
        mantissa.words[static_cast<std::size_t>(clear_word)] &=
            ~(std::uint32_t{1} << clear_shift);
    }
    if (!round_bit || !sticky) {
        return false;
    }
    for (int bit = bit_index - 1; bit >= 0; --bit) {
        const int add_word = bit / 32;
        const int add_shift = 31 - (bit & 31);
        const std::uint32_t mask = std::uint32_t{1} << add_shift;
        std::uint32_t& target = mantissa.words[static_cast<std::size_t>(add_word)];
        if ((target & mask) == 0) {
            target |= mask;
            return false;
        }
        target &= ~mask;
    }
    mantissa.words[0] = 0x80000000u;
    return true;
}

int PackExtendedToFloatOrDouble(const CrtLongDouble80& value, void* output,
    int, bool output_double) {
    const unsigned exponent = value.sign_exponent & 0x7fffu;
    const bool negative = (value.sign_exponent & 0x8000u) != 0;
    long double number = 0.0L;
    if (exponent != 0) {
        const std::uint64_t mantissa =
            (static_cast<std::uint64_t>(value.mantissa_high) << 32) |
            value.mantissa_low;
        number = std::ldexp(static_cast<long double>(mantissa) /
            static_cast<long double>(std::uint64_t{1} << 63),
            static_cast<int>(exponent) - 0x3ffe);
    }
    if (negative) {
        number = -number;
    }
    if (output_double) {
        const double narrowed = static_cast<double>(number);
        if (output != nullptr) {
            std::memcpy(output, &narrowed, sizeof(narrowed));
        }
        return std::isfinite(narrowed) ? 0 : 1;
    }
    const float narrowed = static_cast<float>(number);
    if (output != nullptr) {
        std::memcpy(output, &narrowed, sizeof(narrowed));
    }
    return std::isfinite(narrowed) ? 0 : 1;
}

double ConvertExtendedToDouble(const CrtLongDouble80& value) {
    double out = 0.0;
    PackExtendedToFloatOrDouble(value, &out, 0x40, true);
    return out;
}

float ConvertExtendedToFloat(const CrtLongDouble80& value) {
    float out = 0.0f;
    PackExtendedToFloatOrDouble(value, &out, 0x20, false);
    return out;
}

bool RoundExtendedPrecision(CrtLongDouble80& value, int bit_index) {
    CrtMantissa96 mantissa;
    mantissa.words[0] = value.mantissa_high;
    mantissa.words[1] = value.mantissa_low;
    const bool carry = RoundMantissaToBit(mantissa, bit_index);
    value.mantissa_high = mantissa.words[0];
    value.mantissa_low = mantissa.words[1];
    if (carry) {
        value.sign_exponent = static_cast<std::uint16_t>(
            (value.sign_exponent & 0x8000u) |
            ((value.sign_exponent + 1u) & 0x7fffu));
    }
    return (value.sign_exponent & 0x7fffu) == 0x7fffu;
}

double ParseDecimalStringToDouble(const char* text) {
    return text != nullptr ? std::strtod(text, nullptr) : 0.0;
}

long double ParseDecimalStringToLongDouble(const char* text) {
    return text != nullptr ? std::strtold(text, nullptr) : 0.0L;
}

float ParseDecimalStringToFloat(const char* text) {
    return text != nullptr ? std::strtof(text, nullptr) : 0.0f;
}

void RoundDecimalDigits(std::string& digits, int precision, int& decimal_point) {
    if (precision < 0 || static_cast<std::size_t>(precision) >= digits.size()) {
        return;
    }
    const bool round_up = digits[static_cast<std::size_t>(precision)] > '4';
    digits.resize(static_cast<std::size_t>(precision));
    if (!round_up || digits.empty()) {
        return;
    }
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (*it != '9') {
            ++*it;
            return;
        }
        *it = '0';
    }
    digits.insert(digits.begin(), '1');
    ++decimal_point;
}

void HandleMatherrAndFpuException(int, int error_type, double, double,
    double) {
    set_errno_from_math_type(error_type);
}

bool InitializeLocaleTimeDataFromWin32(LocaleTimeData& data,
    unsigned locale_id) {
    data = DefaultLocaleTimeData();
#ifdef _WIN32
    static constexpr unsigned kAbbrevDayIds[7] = {
        LOCALE_SABBREVDAYNAME7, LOCALE_SABBREVDAYNAME1, LOCALE_SABBREVDAYNAME2,
        LOCALE_SABBREVDAYNAME3, LOCALE_SABBREVDAYNAME4, LOCALE_SABBREVDAYNAME5,
        LOCALE_SABBREVDAYNAME6,
    };
    static constexpr unsigned kDayIds[7] = {
        LOCALE_SDAYNAME7, LOCALE_SDAYNAME1, LOCALE_SDAYNAME2,
        LOCALE_SDAYNAME3, LOCALE_SDAYNAME4, LOCALE_SDAYNAME5,
        LOCALE_SDAYNAME6,
    };
    static constexpr unsigned kAbbrevMonthIds[12] = {
        LOCALE_SABBREVMONTHNAME1, LOCALE_SABBREVMONTHNAME2,
        LOCALE_SABBREVMONTHNAME3, LOCALE_SABBREVMONTHNAME4,
        LOCALE_SABBREVMONTHNAME5, LOCALE_SABBREVMONTHNAME6,
        LOCALE_SABBREVMONTHNAME7, LOCALE_SABBREVMONTHNAME8,
        LOCALE_SABBREVMONTHNAME9, LOCALE_SABBREVMONTHNAME10,
        LOCALE_SABBREVMONTHNAME11, LOCALE_SABBREVMONTHNAME12,
    };
    static constexpr unsigned kMonthIds[12] = {
        LOCALE_SMONTHNAME1, LOCALE_SMONTHNAME2, LOCALE_SMONTHNAME3,
        LOCALE_SMONTHNAME4, LOCALE_SMONTHNAME5, LOCALE_SMONTHNAME6,
        LOCALE_SMONTHNAME7, LOCALE_SMONTHNAME8, LOCALE_SMONTHNAME9,
        LOCALE_SMONTHNAME10, LOCALE_SMONTHNAME11, LOCALE_SMONTHNAME12,
    };
    for (std::size_t index = 0; index < data.day_names.size(); ++index) {
        data.abbreviated_day_names[index] = locale_info_string(locale_id,
            kAbbrevDayIds[index], data.abbreviated_day_names[index].c_str());
        data.day_names[index] = locale_info_string(locale_id, kDayIds[index],
            data.day_names[index].c_str());
    }
    for (std::size_t index = 0; index < data.month_names.size(); ++index) {
        data.abbreviated_month_names[index] = locale_info_string(locale_id,
            kAbbrevMonthIds[index],
            data.abbreviated_month_names[index].c_str());
        data.month_names[index] = locale_info_string(locale_id, kMonthIds[index],
            data.month_names[index].c_str());
    }
    data.am_name = locale_info_string(locale_id, LOCALE_S1159,
        data.am_name.c_str());
    data.pm_name = locale_info_string(locale_id, LOCALE_S2359,
        data.pm_name.c_str());
    data.short_date_format = locale_info_string(locale_id, LOCALE_SSHORTDATE,
        data.short_date_format.c_str());
    data.long_date_format = locale_info_string(locale_id, LOCALE_SLONGDATE,
        data.long_date_format.c_str());
    data.time_format = locale_info_string(locale_id, LOCALE_STIMEFORMAT,
        data.time_format.c_str());
#else
    (void)locale_id;
#endif
    return true;
}

LocaleNumericData InitializeLocaleNumericData(unsigned locale_id) {
    LocaleNumericData data;
#ifdef _WIN32
    data.decimal_point = locale_info_string(locale_id, LOCALE_SDECIMAL, ".");
    data.thousands_separator = locale_info_string(locale_id, LOCALE_STHOUSAND,
        "");
    data.grouping = locale_info_string(locale_id, LOCALE_SGROUPING, "");
    for (char& ch : data.grouping) {
        if (ch >= '0' && ch <= '9') {
            ch = static_cast<char>(ch - '0');
        }
    }
#else
    (void)locale_id;
#endif
    return data;
}

bool InitializeLocaleMonetaryData(LocaleMonetaryData& data,
    unsigned locale_id) {
    return LoadLocaleMonetaryData(data, locale_id);
}

bool LoadLocaleMonetaryData(LocaleMonetaryData& data, unsigned locale_id) {
    data = {};
#ifdef _WIN32
    data.currency_symbol = locale_info_string(locale_id, LOCALE_SCURRENCY, "");
    data.international_currency_symbol = locale_info_string(locale_id,
        LOCALE_SINTLSYMBOL, "");
    data.monetary_decimal_point = locale_info_string(locale_id,
        LOCALE_SMONDECIMALSEP, ".");
    data.monetary_thousands_separator = locale_info_string(locale_id,
        LOCALE_SMONTHOUSANDSEP, "");
    data.monetary_grouping = locale_info_string(locale_id, LOCALE_SMONGROUPING,
        "");
    for (char& ch : data.monetary_grouping) {
        if (ch >= '0' && ch <= '9') {
            ch = static_cast<char>(ch - '0');
        }
    }
    data.positive_sign = locale_info_string(locale_id, LOCALE_SPOSITIVESIGN,
        "");
    data.negative_sign = locale_info_string(locale_id, LOCALE_SNEGATIVESIGN,
        "-");
    data.fractional_digits = locale_info_int(locale_id, LOCALE_ICURRDIGITS, 2);
    data.international_fractional_digits = locale_info_int(locale_id,
        LOCALE_IINTLCURRDIGITS, data.fractional_digits);
    data.positive_format = locale_info_int(locale_id, LOCALE_ICURRENCY, 0);
    data.negative_format = locale_info_int(locale_id, LOCALE_INEGCURR, 0);
#else
    (void)locale_id;
#endif
    return true;
}

void FreeLocaleMonetaryData(LocaleMonetaryData& data) {
    data = {};
}

bool InitializeLocaleCtypeTables(unsigned code_page,
    std::vector<unsigned short>& ctype_table,
    std::vector<unsigned short>& case_map_table) {
    ctype_table.assign(257, 0);
    case_map_table.assign(257, 0);
    std::string bytes;
    bytes.resize(256);
    for (int value = 0; value < 256; ++value) {
        bytes[static_cast<std::size_t>(value)] = static_cast<char>(value);
        case_map_table[static_cast<std::size_t>(value + 1)] =
            static_cast<unsigned short>(value);
    }
    if (!CrtGetStringTypeCompat(1, bytes.data(), 256, ctype_table.data() + 1,
        code_page, 0, false)) {
        return false;
    }
#ifdef _WIN32
    char lower[256]{};
    if (LCMapStringA(LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, bytes.data(), 256,
        lower, 256) != 0) {
        for (int value = 0; value < 256; ++value) {
            case_map_table[static_cast<std::size_t>(value + 1)] =
                static_cast<unsigned char>(lower[value]);
        }
    }
#else
    (void)code_page;
#endif
    return true;
}

int CrtLocaleInitializationNoOp() {
    return 0;
}

bool ResolveLocaleTriple(const char* locale_name, CrtResolvedLocale* out) {
    if (out == nullptr) {
        return false;
    }
    CrtLocaleNameParts parts;
    if (locale_name == nullptr || *locale_name == '\0') {
        UseUserDefaultLocale(*out);
        return true;
    }
    if (!ParseLocaleNameParts(locale_name, parts)) {
        return false;
    }
    std::string language;
    std::string country;
    MapLocaleAlias(parts.language.c_str(), language);
    MapLocaleAlias(parts.country.c_str(), country);
    if (!ResolveLanguageCountryLocale(language.c_str(), country.c_str(), *out)) {
        return false;
    }
    out->code_page = ResolveLocaleCodePage(parts.code_page.c_str(),
        out->sort_locale_id != 0 ? out->sort_locale_id : out->locale_id);
    return out->code_page != 0;
}

bool MapLocaleAlias(const char* input, std::string& out) {
    out = input != nullptr ? input : "";
    std::string lower(out);
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower == "american" || lower == "english") {
        out = "English";
        return true;
    }
    if (lower == "america" || lower == "usa" || lower == "us") {
        out = "United States";
        return true;
    }
    if (lower == "korean" || lower == "korea") {
        out = lower == "korea" ? "Korea" : "Korean";
        return true;
    }
    return !out.empty();
}

bool ResolveLanguageCountryLocale(const char* language, const char* country,
    CrtResolvedLocale& out) {
    UseUserDefaultLocale(out);
    if (language != nullptr) {
        out.language = language;
    }
    if (country != nullptr) {
        out.country = country;
    }
    return true;
}

bool EnumLanguageCountryLocaleProc(const char* locale_id_text,
    const char* language, const char* country, CrtResolvedLocale& out) {
    const unsigned locale_id = ParseHexLocaleId(locale_id_text);
    if (locale_id == 0) {
        return true;
    }
    const std::string locale_language = locale_info_string(locale_id,
        LOCALE_SLANGUAGE, "");
    const std::string locale_country = locale_info_string(locale_id,
        LOCALE_SCOUNTRY, "");
    if ((language == nullptr || CompareMbcsCaseInsensitive(
            locale_language.c_str(), language) == 0) &&
        (country == nullptr || CompareMbcsCaseInsensitive(
            locale_country.c_str(), country) == 0)) {
        out.language = locale_language;
        out.country = locale_country;
        out.locale_id = locale_id;
        out.sort_locale_id = locale_id;
        out.code_page = ResolveLocaleCodePage("ACP", locale_id);
        return false;
    }
    return true;
}

bool ResolveLanguageOnlyLocale(const char* language, CrtResolvedLocale& out) {
    return ResolveLanguageCountryLocale(language, "", out);
}

bool EnumLanguageOnlyLocaleProc(const char* locale_id_text,
    const char* language, CrtResolvedLocale& out) {
    return EnumLanguageCountryLocaleProc(locale_id_text, language, nullptr, out);
}

bool ResolveCountryOnlyLocale(const char* country, CrtResolvedLocale& out) {
    return ResolveLanguageCountryLocale("", country, out);
}

bool EnumCountryOnlyLocaleProc(const char* locale_id_text,
    const char* country, CrtResolvedLocale& out) {
    return EnumLanguageCountryLocaleProc(locale_id_text, nullptr, country, out);
}

void UseUserDefaultLocale(CrtResolvedLocale& out) {
#ifdef _WIN32
    out.locale_id = GetUserDefaultLCID();
    out.sort_locale_id = out.locale_id;
    out.language = locale_info_string(out.locale_id, LOCALE_SLANGUAGE, "");
    out.country = locale_info_string(out.locale_id, LOCALE_SCOUNTRY, "");
    out.code_page = ResolveLocaleCodePage("ACP", out.locale_id);
#else
    out.locale_id = 0;
    out.sort_locale_id = 0;
    out.language = "C";
    out.country.clear();
    out.code_page = 0;
#endif
}

unsigned ResolveLocaleCodePage(const char* code_page_text, unsigned locale_id) {
#ifdef _WIN32
    if (code_page_text == nullptr || *code_page_text == '\0' ||
        std::strcmp(code_page_text, "ACP") == 0) {
        return static_cast<unsigned>(locale_info_int(locale_id,
            LOCALE_IDEFAULTANSICODEPAGE, GetACP()));
    }
    if (std::strcmp(code_page_text, "OCP") == 0) {
        return static_cast<unsigned>(locale_info_int(locale_id,
            LOCALE_IDEFAULTCODEPAGE, GetOEMCP()));
    }
#else
    (void)locale_id;
#endif
    return static_cast<unsigned>(std::strtoul(
        code_page_text != nullptr ? code_page_text : "0", nullptr, 10));
}

bool IsNonCountryLocale(unsigned locale_id) {
#ifdef _WIN32
    static constexpr unsigned kNonCountryLanguages[] = {
        LANG_ARABIC, LANG_CHINESE, LANG_ENGLISH, LANG_FRENCH, LANG_GERMAN,
        LANG_ITALIAN, LANG_PORTUGUESE, LANG_SERBIAN, LANG_SPANISH,
    };
    const unsigned primary = PRIMARYLANGID(LANGIDFROMLCID(locale_id));
    return std::find(std::begin(kNonCountryLanguages),
        std::end(kNonCountryLanguages), primary) !=
        std::end(kNonCountryLanguages);
#else
    (void)locale_id;
    return true;
#endif
}

bool IsPrimaryLanguageLocale(unsigned locale_id, bool strict_primary) {
#ifdef _WIN32
    const unsigned user = GetUserDefaultLCID();
    if (strict_primary) {
        return PRIMARYLANGID(LANGIDFROMLCID(locale_id)) ==
            PRIMARYLANGID(LANGIDFROMLCID(user));
    }
    return (locale_id & 0x03ffu) == (user & 0x03ffu);
#else
    (void)locale_id;
    (void)strict_primary;
    return true;
#endif
}

bool IsWindowsNtPlatform() {
#ifdef _WIN32
    OSVERSIONINFOA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return GetVersionExA(&version) != FALSE && version.dwPlatformId == VER_PLATFORM_WIN32_NT;
#else
    return false;
#endif
}

unsigned ParseHexLocaleId(const char* text) {
    if (text == nullptr) {
        return 0;
    }
    return static_cast<unsigned>(std::strtoul(text, nullptr, 16));
}

bool ValidateReadPointer(const void* memory, std::size_t bytes) {
    if (memory == nullptr && bytes != 0) {
        return false;
    }
#ifdef _WIN32
    return IsBadReadPtr(memory, bytes) == FALSE;
#else
    (void)bytes;
    return true;
#endif
}

bool ValidateWritePointer(void* memory, std::size_t bytes) {
    if (memory == nullptr && bytes != 0) {
        return false;
    }
#ifdef _WIN32
    return IsBadWritePtr(memory, bytes) == FALSE;
#else
    (void)bytes;
    return true;
#endif
}

long long CrtSeekFileDescriptor64Locked(int file_descriptor, long long offset,
    int origin) {
    LockCrtFileDescriptor(file_descriptor);
    const long long result = CrtSeekFileDescriptor64NoLock(file_descriptor,
        offset, origin);
    UnlockCrtRuntime(0);
    return result;
}

long long CrtSeekFileDescriptor64NoLock(int file_descriptor, long long offset,
    int origin) {
#ifdef _WIN32
    return _lseeki64(file_descriptor, offset, origin);
#else
    return CrtSeekFileDescriptorNoLock(file_descriptor,
        static_cast<long>(offset), origin);
#endif
}

int CrtChangeFileSizeLocked(int file_descriptor, long long size) {
    LockCrtFileDescriptor(file_descriptor);
    const int result = CrtChangeFileSizeNoLock(file_descriptor, size);
    UnlockCrtRuntime(0);
    return result;
}

int CrtChangeFileSizeNoLock(int file_descriptor, long long size) {
    if (size < 0) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
#ifdef _WIN32
    const intptr_t os_handle_value = CrtGetOsFileHandle(file_descriptor);
    if (os_handle_value == -1) {
        *CrtErrnoPointer() = EBADF;
        return -1;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(os_handle_value);
    LARGE_INTEGER current{};
    LARGE_INTEGER target{};
    target.QuadPart = size;
    LARGE_INTEGER zero{};
    if (SetFilePointerEx(handle, zero, &current, FILE_CURRENT) == FALSE ||
        SetFilePointerEx(handle, target, nullptr, FILE_BEGIN) == FALSE ||
        SetEndOfFile(handle) == FALSE) {
        *CrtDosErrnoPointer() = GetLastError();
        *CrtErrnoPointer() = EACCES;
        return -1;
    }
    SetFilePointerEx(handle, current, nullptr, FILE_BEGIN);
    return 0;
#else
    (void)file_descriptor;
    (void)size;
    *CrtErrnoPointer() = ENOSYS;
    return -1;
#endif
}

bool CrtIsMbbAlpha(unsigned character) {
    return CrtTestMbcsByteType(character, 0, 0x01);
}

bool CrtIsMbbAlnum(unsigned character) {
    return CrtTestMbcsByteType(character, 0, 0x03);
}

bool CrtIsMbbPunct(unsigned character) {
    return CrtTestMbcsByteType(character, 0, 0x02);
}

bool CrtIsMbbGraph(unsigned character) {
    return CrtTestMbcsByteType(character, 0x0107, 0x01);
}

bool CrtIsMbbPrint(unsigned character) {
    return CrtTestMbcsByteType(character, 0x0103, 0x01);
}

bool CrtIsMbbKalnum(unsigned character) {
    return CrtTestMbcsByteType(character, 0x0117, 0x03);
}

bool CrtIsMbbKprint(unsigned character) {
    return CrtTestMbcsByteType(character, 0x0157, 0x03);
}

bool CrtIsMbbPunctOrKana(unsigned character) {
    return CrtTestMbcsByteType(character, 0x0010, 0x02);
}

bool CrtIsMbcsLeadByte(unsigned character) {
    return CrtTestMbcsByteType(character, 0, 0x04);
}

bool CrtIsMbcsTrailByte(unsigned character) {
    return CrtTestMbcsByteType(character, 0, 0x08);
}

bool CrtTestMbcsByteType(unsigned character, unsigned ctype_mask,
    unsigned mbcs_mask) {
    const unsigned ch = character & 0xffu;
#ifdef _WIN32
    if ((mbcs_mask & 0x04u) != 0 &&
        IsDBCSLeadByteEx(GetActiveMbcsCodePage(), static_cast<BYTE>(ch)) != FALSE) {
        return true;
    }
    if ((mbcs_mask & 0x08u) != 0 && ch >= 0x40 && ch != 0x7f && ch <= 0xfcu) {
        return true;
    }
#endif
    if (ctype_mask != 0 && CrtIsCharType(ch, ctype_mask) != 0) {
        return true;
    }
    if ((mbcs_mask & 0x01u) != 0 && std::isalpha(static_cast<unsigned char>(ch)) != 0) {
        return true;
    }
    if ((mbcs_mask & 0x02u) != 0 && std::ispunct(static_cast<unsigned char>(ch)) != 0) {
        return true;
    }
    return false;
}

int CrtMbtowcLocked(wchar_t* destination, const char* source,
    std::size_t bytes) {
    LockCrtRuntime(0x13);
    const int result = CrtMbtowcNoLock(destination, source, bytes);
    UnlockCrtRuntime(0x13);
    return result;
}

int CrtMbtowcNoLock(wchar_t* destination, const char* source,
    std::size_t bytes) {
    if (source == nullptr || bytes == 0) {
        return 0;
    }
    if (*source == '\0') {
        if (destination != nullptr) {
            *destination = L'\0';
        }
        return 0;
    }
#ifdef _WIN32
    const int result = MultiByteToWideChar(GetActiveMbcsCodePage(),
        MB_ERR_INVALID_CHARS, source, static_cast<int>(bytes), destination,
        destination != nullptr ? 1 : 0);
    if (result == 0) {
        *CrtErrnoPointer() = EILSEQ;
        return -1;
    }
    return static_cast<unsigned char>(*source) < 0x80 ? 1 :
        static_cast<int>(std::min<std::size_t>(bytes, 2));
#else
    std::mbstate_t state{};
    const std::size_t result = std::mbrtowc(destination, source, bytes, &state);
    if (result == static_cast<std::size_t>(-1) ||
        result == static_cast<std::size_t>(-2)) {
        *CrtErrnoPointer() = EILSEQ;
        return -1;
    }
    return static_cast<int>(result);
#endif
}

wint_t CrtFputwcLocked(wchar_t character, FILE* stream) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return WEOF;
    }
    LockCrtStream(stream);
    const wint_t result = CrtFputwcNoLock(character, stream);
    UnlockCrtStream(stream);
    return result;
}

wint_t CrtFputwcNoLock(wchar_t character, FILE* stream) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return WEOF;
    }
    return std::fputwc(character, stream);
}

void CrtFputwcThunk(wchar_t character, FILE* stream) {
    (void)CrtFputwcLocked(character, stream);
}

double CrtScaleDoubleByPowerOfTwo(double value, int exponent) {
    return ScaleDoubleByPowerOfTwo(value, exponent);
}

CrtDecimalConversion ConvertExtendedToDecimalString(
    const CrtLongDouble80& value, int precision, bool fixed_digits) {
    (void)fixed_digits;
    return ConvertDoubleToDecimalString(ConvertExtendedToDouble(value),
        precision);
}

bool RebuildEnvironmentFromWide(const std::vector<std::wstring>& wide_environment,
    std::vector<std::string>& ansi_environment) {
    ansi_environment.clear();
    for (const std::wstring& wide : wide_environment) {
#ifdef _WIN32
        const int bytes = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1,
            nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) {
            return false;
        }
        std::string ansi(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, ansi.data(), bytes,
            nullptr, nullptr);
        if (!ansi.empty() && ansi.back() == '\0') {
            ansi.pop_back();
        }
        ansi_environment.push_back(ansi);
#else
        std::string ansi(wide.begin(), wide.end());
        ansi_environment.push_back(ansi);
#endif
    }
    return true;
}

CrtLongDouble80 AccumulateDecimalDigitsToExtended(const char* digits,
    int count) {
    long double value = 0.0L;
    if (digits != nullptr) {
        for (int index = 0; index < count && digits[index] != '\0'; ++index) {
            if (digits[index] >= '0' && digits[index] <= '9') {
                value = value * 10.0L + static_cast<int>(digits[index] - '0');
            }
        }
    }
    return ConvertDoubleToLongDoubleBits(static_cast<double>(value));
}

CrtParsedFloat ParseFloatingPointStringToExtended(const char* text,
    bool) {
    CrtParsedFloat result;
    if (text == nullptr) {
        return result;
    }
    char* end = nullptr;
    const long double value = std::strtold(text, &end);
    result.end = end != nullptr ? end : text;
    result.value = ConvertDoubleToLongDoubleBits(static_cast<double>(value));
    if (end == text) {
        result.flags |= 0x04;
    } else if (value == 0.0L) {
        result.flags |= 0x01;
    } else if (!std::isfinite(static_cast<double>(value))) {
        result.flags |= 0x02;
    }
    return result;
}

unsigned ParseAndRoundFloatingPointString(const char* text, int precision,
    CrtLongDouble80& out) {
    CrtParsedFloat parsed = ParseFloatingPointStringToExtended(text, true);
    out = parsed.value;
    if (RoundExtendedPrecision(out, precision)) {
        parsed.flags |= 0x02;
    }
    return parsed.flags;
}

std::string LoadLocaleStringValue(unsigned locale_id, unsigned locale_type,
    bool string_value) {
    if (!string_value) {
        return std::to_string(locale_info_int(locale_id, locale_type, 0));
    }
    return locale_info_string(locale_id, locale_type, "");
}

LocaleMonetaryData* GetLocaleMonetaryDataPointer() {
    static LocaleMonetaryData data = [] {
        LocaleMonetaryData initial;
        InitializeLocaleMonetaryData(initial, 0);
        return initial;
    }();
    return &data;
}

bool GetStringTypeWideCompat(unsigned info_type, const wchar_t* text, int chars,
    unsigned short* types, unsigned code_page, unsigned locale_id) {
    if (text == nullptr || types == nullptr) {
        return false;
    }
#ifdef _WIN32
    if (GetStringTypeW(static_cast<DWORD>(info_type), text, chars,
        reinterpret_cast<LPWORD>(types)) != FALSE) {
        return true;
    }
    const UINT mapped_code_page = code_page != 0 ? code_page : CP_ACP;
    const int bytes = WideCharToMultiByte(mapped_code_page, 0, text, chars,
        nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return false;
    }
    std::string narrow(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(mapped_code_page, 0, text, chars, narrow.data(),
        bytes, nullptr, nullptr) == 0) {
        return false;
    }
    return CrtGetStringTypeCompat(info_type, narrow.data(), bytes, types,
        mapped_code_page, locale_id, false);
#else
    (void)code_page;
    (void)locale_id;
    for (int index = 0; index < chars; ++index) {
        types[index] = static_cast<unsigned short>(
            std::iswalpha(text[index]) ? 0x0100 : 0);
    }
    return true;
#endif
}

int CompareLocaleStringsCaseInsensitivePrefix(const char* left,
    const char* right, int count) {
    if (count <= 0) {
        return 0;
    }
    for (int index = 0; index < count; ++index) {
        const unsigned char lhs = left != nullptr ? left[index] : 0;
        const unsigned char rhs = right != nullptr ? right[index] : 0;
        const int l = std::tolower(lhs);
        const int r = std::tolower(rhs);
        if (l != r || l == 0 || r == 0) {
            return l < r ? -1 : (l > r ? 1 : 0);
        }
    }
    return 0;
}

int CrtSetFileTextModeLocked(int file_descriptor, int mode) {
    LockCrtFileDescriptor(file_descriptor);
    const int previous = CrtSetFileTextModeNoLock(file_descriptor, mode);
    UnlockCrtRuntime(0);
    return previous;
}

int CrtSetFileTextModeNoLock(int file_descriptor, int mode) {
#ifdef _WIN32
    if (mode != _O_TEXT && mode != _O_BINARY) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
    return _setmode(file_descriptor, mode);
#else
    (void)file_descriptor;
    (void)mode;
    return _O_TEXT;
#endif
}

wint_t CrtFlushWideStreamBuffer(wchar_t character, FILE* stream) {
    return CrtFputwcNoLock(character, stream);
}

void MultiplyExtendedTemporary(CrtLongDouble80& value,
    const CrtLongDouble80& multiplier) {
    const long double product =
        static_cast<long double>(ConvertExtendedToDouble(value)) *
        static_cast<long double>(ConvertExtendedToDouble(multiplier));
    value = ConvertDoubleToLongDoubleBits(static_cast<double>(product));
}

void ScaleExtendedByPowerOfTen(CrtLongDouble80& value, int exponent,
    bool initialize) {
    if (initialize) {
        value = ConvertDoubleToLongDoubleBits(1.0);
    }
    const long double scaled =
        static_cast<long double>(ConvertExtendedToDouble(value)) *
        std::pow(10.0L, static_cast<long double>(exponent));
    value = ConvertDoubleToLongDoubleBits(static_cast<double>(scaled));
}

int CrtSetEnvironmentEntry(const char* entry, bool update_process) {
    if (entry == nullptr) {
        return -1;
    }
    const char* equals = std::strchr(entry, '=');
    if (equals == nullptr || equals == entry) {
        return -1;
    }
    std::string name(entry, equals);
    const char* value = equals + 1;
#ifdef _WIN32
    if (update_process) {
        if (*value == '\0') {
            return _putenv_s(name.c_str(), "") == 0 ? 0 : -1;
        }
        return _putenv_s(name.c_str(), value) == 0 ? 0 : -1;
    }
#else
    (void)update_process;
#endif
    return *value == '\0' ? 0 : 0;
}

int GetLocaleInfoWideCompat(unsigned locale_id, unsigned locale_type,
    wchar_t* destination, int destination_chars, unsigned code_page) {
#ifdef _WIN32
    const LCID mapped_locale = locale_id != 0 ? locale_id : LOCALE_USER_DEFAULT;
    if (GetLocaleInfoW(mapped_locale, static_cast<LCTYPE>(locale_type),
        destination, destination_chars) != 0) {
        return destination_chars == 0
            ? GetLocaleInfoW(mapped_locale, static_cast<LCTYPE>(locale_type),
                nullptr, 0)
            : static_cast<int>(std::wcslen(destination)) + 1;
    }
    std::string ansi = locale_info_string(mapped_locale, locale_type, "");
    if (destination_chars == 0) {
        return MultiByteToWideChar(code_page != 0 ? code_page : CP_ACP, 0,
            ansi.c_str(), -1, nullptr, 0);
    }
    return MultiByteToWideChar(code_page != 0 ? code_page : CP_ACP, 0,
        ansi.c_str(), -1, destination, destination_chars);
#else
    (void)locale_id;
    (void)locale_type;
    (void)code_page;
    if (destination != nullptr && destination_chars > 0) {
        destination[0] = L'\0';
    }
    return 0;
#endif
}

int GetLocaleInfoAnsiCompat(unsigned locale_id, unsigned locale_type,
    char* destination, int destination_chars, unsigned code_page) {
#ifdef _WIN32
    const LCID mapped_locale = locale_id != 0 ? locale_id : LOCALE_USER_DEFAULT;
    if (GetLocaleInfoA(mapped_locale, static_cast<LCTYPE>(locale_type),
        destination, destination_chars) != 0) {
        return destination_chars == 0
            ? GetLocaleInfoA(mapped_locale, static_cast<LCTYPE>(locale_type),
                nullptr, 0)
            : static_cast<int>(std::strlen(destination)) + 1;
    }
    const int wide_chars = GetLocaleInfoW(mapped_locale,
        static_cast<LCTYPE>(locale_type), nullptr, 0);
    if (wide_chars == 0) {
        return 0;
    }
    std::vector<wchar_t> wide(static_cast<std::size_t>(wide_chars));
    if (GetLocaleInfoW(mapped_locale, static_cast<LCTYPE>(locale_type),
        wide.data(), wide_chars) == 0) {
        return 0;
    }
    return WideCharToMultiByte(code_page != 0 ? code_page : CP_ACP, 0,
        wide.data(), -1, destination, destination_chars, nullptr, nullptr);
#else
    (void)locale_id;
    (void)locale_type;
    (void)code_page;
    if (destination != nullptr && destination_chars > 0) {
        destination[0] = '\0';
    }
    return 0;
#endif
}

int CrtStreamFileDescriptor(FILE* stream) {
    if (stream == nullptr) {
        *CrtErrnoPointer() = EINVAL;
        return -1;
    }
#ifdef _WIN32
    return _fileno(stream);
#else
    return -1;
#endif
}

void InitializeCrtFileTable() {
}

void ShutdownCrtStdio() {
    std::fflush(nullptr);
}

void LockCrtStream(FILE*) {
}

void LockCrtStreamByIndex(int, FILE*) {
}

void UnlockCrtStream(FILE*) {
}

void UnlockCrtStreamByIndex(int, FILE*) {
}

int CrtCloseFileDescriptor(int file_descriptor) {
#ifdef _WIN32
    return _close(file_descriptor);
#else
    (void)file_descriptor;
    errno = ENOSYS;
    return -1;
#endif
}

void CrtFreeStreamBuffer(FILE* stream) {
    if (stream != nullptr) {
        std::fflush(stream);
    }
}

int CrtFlushStream(FILE* stream) {
    return stream == nullptr ? std::fflush(nullptr) : std::fflush(stream);
}

int CrtFlushStreamNoLock(FILE* stream) {
    return CrtFlushStream(stream);
}

int CrtFlushStreamBuffer(FILE* stream) {
    return CrtFlushStream(stream);
}

int CrtFlushAllStreamsOnExit() {
    return CrtFlushAllStreams(true);
}

int CrtFlushAllStreams(bool) {
    return std::fflush(nullptr);
}

bool CrtInstallTemporaryStreamBuffer(FILE* stream) {
    if (stream == nullptr) {
        return false;
    }
    if (g_temporary_stream_buffers.find(stream) !=
        g_temporary_stream_buffers.end()) {
        return true;
    }
    auto [it, inserted] = g_temporary_stream_buffers.emplace(
        stream, std::vector<char>(BUFSIZ));
    (void)inserted;
    if (std::setvbuf(stream, it->second.data(), _IOFBF,
            it->second.size()) != 0) {
        g_temporary_stream_buffers.erase(it);
        return false;
    }
    return true;
}

void CrtRemoveTemporaryStreamBuffer(bool installed, FILE* stream) {
    if (installed && stream != nullptr) {
        std::fflush(stream);
        auto it = g_temporary_stream_buffers.find(stream);
        if (it != g_temporary_stream_buffers.end()) {
            std::setvbuf(stream, nullptr, _IONBF, 0);
            g_temporary_stream_buffers.erase(it);
        }
    }
}

bool InitializeCrtThreadData() {
    InitializeCrtThreadDataBlock(g_thread_data);
    return true;
}

void ShutdownCrtThreadData() {
}

void InitializeCrtThreadDataBlock(CrtThreadData& data) {
    data.thread_id = CrtCurrentThreadId();
    data.handle_marker = 0xffffffffu;
}

CrtThreadData& CrtGetThreadData() {
    if (g_thread_data.thread_id == 0) {
        InitializeCrtThreadDataBlock(g_thread_data);
    }
    return g_thread_data;
}

void FreeCrtThreadData(CrtThreadData*) {
}

unsigned CrtCurrentThreadId() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return 0;
#endif
}

void* CrtCurrentThreadHandle() {
#ifdef _WIN32
    return GetCurrentThread();
#else
    return nullptr;
#endif
}

FILE* CrtOpenFileStream(const char* path, const char* mode, int share_flags,
    FILE* stream) {
    (void)stream;
    return CrtFsopen(path, mode, share_flags);
}

FILE* CrtAllocateStreamSlot() {
    return nullptr;
}

void InitializeCrtTimeZoneOnce() {
    if (!g_time_zone_initialized) {
        RefreshCrtTimeZone();
        g_time_zone_initialized = true;
    }
}

void RefreshCrtTimeZone() {
    RebuildCrtTimeZoneFromEnvironment();
}

void RebuildCrtTimeZoneFromEnvironment() {
    g_time_zone_seconds = 0;
    g_daylight_bias_seconds = 0;
    g_daylight_savings_enabled = false;
    g_standard_time_name = "STD";
    g_daylight_time_name = "DST";

    const char* tz = std::getenv("TZ");
    if (tz != nullptr && tz[0] != '\0') {
        const std::size_t standard_chars = std::min<std::size_t>(3, std::strlen(tz));
        g_standard_time_name.assign(tz, tz + standard_chars);
        const char* cursor = tz + standard_chars;
        const bool negative = *cursor == '-';
        if (*cursor == '-' || *cursor == '+') {
            ++cursor;
        }
        const int hours = std::atoi(cursor);
        while (*cursor >= '0' && *cursor <= '9') {
            ++cursor;
        }
        int minutes = 0;
        int seconds = 0;
        if (*cursor == ':') {
            minutes = std::atoi(++cursor);
            while (*cursor >= '0' && *cursor <= '9') {
                ++cursor;
            }
            if (*cursor == ':') {
                seconds = std::atoi(++cursor);
                while (*cursor >= '0' && *cursor <= '9') {
                    ++cursor;
                }
            }
        }
        g_time_zone_seconds = ((hours * 60 + minutes) * 60 + seconds) *
            (negative ? -1 : 1);
        if (*cursor != '\0') {
            g_daylight_savings_enabled = true;
            g_daylight_time_name.assign(cursor,
                cursor + std::min<std::size_t>(3, std::strlen(cursor)));
        }
        return;
    }

#ifdef _WIN32
    TIME_ZONE_INFORMATION info{};
    const DWORD result = GetTimeZoneInformation(&info);
    if (result != TIME_ZONE_ID_INVALID) {
        g_time_zone_seconds = static_cast<long>(info.Bias) * 60;
        g_daylight_bias_seconds = static_cast<long>(info.DaylightBias) * 60;
        g_daylight_savings_enabled =
            info.DaylightDate.wMonth != 0 && info.StandardDate.wMonth != 0;

        char buffer[64]{};
        if (WideCharToMultiByte(CP_ACP, 0, info.StandardName, -1,
                buffer, sizeof(buffer), nullptr, nullptr) != 0) {
            g_standard_time_name = buffer;
        }
        buffer[0] = '\0';
        if (WideCharToMultiByte(CP_ACP, 0, info.DaylightName, -1,
                buffer, sizeof(buffer), nullptr, nullptr) != 0) {
            g_daylight_time_name = buffer;
        }
    }
#endif
}

bool CrtIsDaylightSavingsTime(const std::tm& value) {
    LockCrtRuntime(0x0b);
    const bool result = CrtIsDaylightSavingsTimeLocked(value);
    UnlockCrtRuntime(0x0b);
    return result;
}

bool CrtIsDaylightSavingsTimeLocked(const std::tm& value) {
    InitializeCrtTimeZoneOnce();
    if (!g_daylight_savings_enabled) {
        return false;
    }
    if (value.tm_isdst >= 0) {
        return value.tm_isdst > 0;
    }
    std::tm copy = value;
    if (std::mktime(&copy) == static_cast<std::time_t>(-1)) {
        return false;
    }
    return copy.tm_isdst > 0;
}

void ComputeDaylightTransitionDay(bool, bool, int, int, int, int, int,
    int, int, int, int) {
}

std::time_t CrtEncodeLocalTimeFields(int year, int month, int day, int hour,
    int minute, int second, int daylight_savings) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    value.tm_isdst = daylight_savings;
    return std::mktime(&value);
}

void InitializeCrtLockTable() {
}

void ShutdownCrtLockTable() {
}

void LockCrtRuntime(int lock_index) {
    if (lock_index >= 0 && lock_index < static_cast<int>(g_crt_locks.size())) {
        g_crt_locks[static_cast<std::size_t>(lock_index)].lock();
    }
}

void UnlockCrtRuntime(int lock_index) {
    if (lock_index >= 0 && lock_index < static_cast<int>(g_crt_locks.size())) {
        g_crt_locks[static_cast<std::size_t>(lock_index)].unlock();
    }
}

void CrtFatalAppExit(const char* message) {
#ifdef _WIN32
    FatalAppExitA(0, message != nullptr ? message : "Fatal CRT error");
    ExitProcess(0xff);
#else
    std::fprintf(stderr, "%s\n", message != nullptr ? message : "Fatal CRT error");
    std::exit(0xff);
#endif
}

CrtNewHandler CrtGetNewHandler() {
    return g_new_handler;
}

void UnlockHeapAfterSbhAlloc() {
    UnlockCrtRuntime(9);
}

void* FinishSbhAllocOrFallback(std::size_t size) {
    return HeapAllocRoundedFallback(size);
}

void UnlockHeapAfterLookasideAlloc() {
    UnlockCrtRuntime(9);
}

void* FinishLookasideAllocOrHeap(std::size_t size) {
    return HeapAllocRoundedFallback(size);
}

void* HeapAllocRoundedFallback(std::size_t size) {
    const std::size_t rounded = round_heap_allocation_size(size);
    return rounded != 0 ? std::malloc(rounded) : nullptr;
}

int CrtHeapAddNoOp() {
    return 1;
}

void* CrtHeapReallocInPlace(void* memory, std::size_t size) {
    const std::size_t rounded = round_heap_allocation_size(size);
    return rounded != 0 ? CrtReallocOrExpand(memory, rounded, false) : nullptr;
}

void UnlockHeapAfterSbhRealloc() {
    UnlockCrtRuntime(9);
}

void* FinishSbhReallocFallback(void* memory, std::size_t size) {
    const std::size_t rounded = round_heap_allocation_size(size);
    return rounded != 0 ? std::realloc(memory, rounded) : nullptr;
}

void UnlockHeapAfterLookasideRealloc() {
    UnlockCrtRuntime(9);
}

void* FinishLookasideReallocFallback(void* memory, std::size_t size) {
    const std::size_t rounded = round_heap_allocation_size(size);
    return rounded != 0 ? std::realloc(memory, rounded) : nullptr;
}

void* CrtHeapRealloc(void* memory, std::size_t size) {
    if (memory == nullptr) {
        return HeapAllocRoundedFallback(size);
    }
    if (size == 0) {
        CrtHeapFree(memory);
        return nullptr;
    }
    return retry_realloc_with_new_handler(memory, size);
}

void UnlockHeapAfterSbhReallocRetry() {
    UnlockCrtRuntime(9);
}

void* RetrySbhReallocAfterNewHandler(void* memory, std::size_t size) {
    return retry_realloc_with_new_handler(memory, size);
}

void UnlockHeapAfterLookasideReallocRetry() {
    UnlockCrtRuntime(9);
}

void* RetryLookasideReallocAfterNewHandler(void* memory, std::size_t size) {
    return retry_realloc_with_new_handler(memory, size);
}

void RestoreHeapExceptionFrame() {
}

void CrtHeapFree(void* memory) {
    std::free(memory);
}

void UnlockHeapAfterSbhFree() {
    UnlockCrtRuntime(9);
}

void FinishSbhFreeFallback(void* memory) {
    std::free(memory);
}

void UnlockHeapAfterLookasideFree() {
    UnlockCrtRuntime(9);
}

void FinishLookasideFreeFallback(void* memory) {
    std::free(memory);
}

void UnlockHeapAfterSbhCheck() {
    UnlockCrtRuntime(9);
}

void UnlockHeapAfterLookasideCheck() {
    UnlockCrtRuntime(9);
}

int FinishHeapValidate() {
#ifdef _WIN32
    if (HeapValidate(GetProcessHeap(), 0, nullptr) == 0) {
        return -4;
    }
#endif
    return -2;
}

std::size_t CrtGetSmallBlockThreshold() {
    return g_small_block_threshold;
}

bool CrtSetSmallBlockThreshold(std::size_t threshold) {
    if (threshold <= 0x3f8) {
        g_small_block_threshold = threshold;
        return true;
    }
    const std::size_t rounded = round_heap_threshold(threshold);
    if (rounded <= 0x780) {
        g_lookaside_threshold = rounded;
        return true;
    }
    return false;
}

void* SbhFindRegionForPointer(void* memory) {
    return memory;
}

bool SbhValidatePointerInRegion(void*, void* memory) {
    return memory != nullptr;
}

void SbhFreeBlock(void*, void* memory) {
    std::free(memory);
}

void* SbhAllocateBlock(std::size_t size) {
    return HeapAllocRoundedFallback(size);
}

void* SbhCreateRegion() {
    return std::malloc(0x140);
}

int SbhCommitRegionPage(void* region) {
    return region != nullptr ? 1 : 0;
}

bool SbhResizeBlock(void*, void* memory, std::size_t size) {
    if (memory == nullptr) {
        return false;
    }
    const std::size_t current_size = CrtDebugMemorySize(memory);
    return current_size != 0 ? size <= current_size : size <= g_small_block_threshold;
}

void SbhReleaseDeferredPage() {
}

int SbhValidateHeap() {
    return 0;
}

std::size_t LookasideGetThreshold() {
    return g_lookaside_threshold;
}

bool LookasideSetThreshold(std::size_t threshold) {
    const std::size_t rounded = round_heap_threshold(threshold);
    if (rounded <= 0x780) {
        g_lookaside_threshold = rounded;
        return true;
    }
    return false;
}

void* LookasideCreateRegion() {
    return std::malloc(0x2020);
}

void LookasideDestroyRegion(void* region) {
    std::free(region);
}

void LookasideReleaseFreePages(int) {
}

void* LookasideFindBlock(void* memory, void** region, unsigned* page) {
    if (region != nullptr) {
        *region = nullptr;
    }
    if (page != nullptr) {
        *page = 0;
    }
    return memory;
}

void LookasideFreeBlock(void*, unsigned, void* block) {
    std::free(block);
}

void* LookasideAllocateBlock(unsigned units) {
    const std::size_t bytes = lookaside_units_to_bytes(units);
    return bytes != 0 ? HeapAllocRoundedFallback(bytes) : nullptr;
}

void* LookasideAllocateFromPage(void*, unsigned free_units, unsigned units) {
    if (free_units != 0 && units > free_units) {
        return nullptr;
    }
    return LookasideAllocateBlock(units);
}

bool LookasideResizeBlockInPlace(void*, void*, void* block, unsigned units) {
    if (block == nullptr) {
        return false;
    }
    const std::size_t bytes = lookaside_units_to_bytes(units);
    if (bytes == 0) {
        return false;
    }
    const std::size_t current_size = CrtDebugMemorySize(block);
    return current_size != 0 ? bytes <= current_size : bytes <= g_lookaside_threshold;
}

int LookasideValidateHeap() {
    return 0;
}

unsigned GetProcessSubsystemVersion() {
#ifdef _WIN32
    const auto module = reinterpret_cast<const unsigned char*>(GetModuleHandleA(nullptr));
    if (module != nullptr) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                module + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) {
                return static_cast<unsigned>(nt->OptionalHeader.MajorSubsystemVersion) |
                    (static_cast<unsigned>(nt->OptionalHeader.MinorSubsystemVersion) << 8);
            }
        }
    }
#endif
    return 0x0006;
}

int SelectCrtHeapMode() {
#ifdef _WIN32
    OSVERSIONINFOEXA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (GetVersionExA(reinterpret_cast<OSVERSIONINFOA*>(&version)) != FALSE &&
        version.wProductType == VER_NT_SERVER && version.dwMajorVersion >= 5) {
        return 1;
    }
#endif

    if (const char* env = std::getenv("__MSVCRT_HEAP_SELECT")) {
        std::string value(env);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        const char* cursor = value.c_str();
        while (*cursor != '\0' && *cursor != '1' && *cursor != '2' && *cursor != '3') {
            ++cursor;
        }
        if (*cursor >= '1' && *cursor <= '3') {
            return *cursor - '0';
        }
    }

    return (GetProcessSubsystemVersion() & 0xffu) < 6 ? 2 : 3;
}

bool InitializeCrtHeap(bool) {
    const int mode = SelectCrtHeapMode();
    if (mode == 2) {
        g_lookaside_threshold = 0x780;
    } else if (mode == 3) {
        g_small_block_threshold = 0x3f8;
    }
    return mode >= 1 && mode <= 3;
}

void ShutdownCrtHeap() {
}

unsigned CrtIsCharType(int character, unsigned mask) {
    const unsigned ch = static_cast<unsigned>(character) & 0xffu;
    unsigned flags = 0;
    if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0001;
    }
    if (std::islower(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0002;
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0004;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0008;
    }
    if (std::ispunct(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0010;
    }
    if (std::iscntrl(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0020;
    }
    if (ch == ' ' || ch == '\t') {
        flags |= 0x0040;
    }
    if (std::isxdigit(static_cast<unsigned char>(ch)) != 0) {
        flags |= 0x0080;
    }
#ifdef _WIN32
    if (IsDBCSLeadByteEx(GetActiveMbcsCodePage(), static_cast<BYTE>(ch)) != FALSE) {
        flags |= 0x8000;
    }
#endif
    return flags & mask;
}

CrtOnExitFunction CrtRegisterOnExitFunction(CrtOnExitFunction function) {
    if (function == nullptr) {
        return nullptr;
    }
    LockCrtRuntime(0x0d);
    g_onexit_functions.push_back(function);
    UnlockCrtRuntime(0x0d);
    return function;
}

int CrtAtexit(CrtOnExitFunction function) {
    return CrtRegisterOnExitFunction(function) != nullptr ? 0 : -1;
}

void InitializeCrtOnExitTable() {
    LockCrtRuntime(0x0d);
    g_onexit_functions.clear();
    UnlockCrtRuntime(0x0d);
}

void CrtSrand(unsigned seed) {
    std::srand(seed);
}

int _rand() {
    return std::rand();
}

char* StrUpperAscii(char* text) {
    if (text == nullptr) {
        return nullptr;
    }
    for (char* cursor = text; *cursor != '\0'; ++cursor) {
        *cursor = static_cast<char>(
            std::toupper(static_cast<unsigned char>(*cursor)));
    }
    return text;
}

int StrCaseCompareAscii(const char* lhs, const char* rhs) {
    if (lhs == rhs) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }
    while (*lhs != '\0' || *rhs != '\0') {
        const int left = std::tolower(static_cast<unsigned char>(*lhs));
        const int right = std::tolower(static_cast<unsigned char>(*rhs));
        if (left != right) {
            return left < right ? -1 : 1;
        }
        if (*lhs != '\0') {
            ++lhs;
        }
        if (*rhs != '\0') {
            ++rhs;
        }
    }
    return 0;
}

char* StrTokDelimiterSet(char* value, const char* delimiters) {
    if (delimiters == nullptr) {
        return nullptr;
    }
    char* cursor = value != nullptr ? value : g_strtok_delimiter_context;
    if (cursor == nullptr) {
        return nullptr;
    }

    cursor += std::strspn(cursor, delimiters);
    if (*cursor == '\0') {
        g_strtok_delimiter_context = nullptr;
        return nullptr;
    }

    char* token = cursor;
    cursor += std::strcspn(cursor, delimiters);
    if (*cursor == '\0') {
        g_strtok_delimiter_context = nullptr;
    } else {
        *cursor = '\0';
        g_strtok_delimiter_context = cursor + 1;
    }
    return token;
}

void __initterm(CrtOnExitFunction* first, CrtOnExitFunction* last) {
    if (first == nullptr || last == nullptr) {
        return;
    }
    for (CrtOnExitFunction* cursor = first; cursor < last; ++cursor) {
        if (*cursor != nullptr) {
            (*cursor)();
        }
    }
}

void __exit(int code) {
    CrtDoExit(code, true, false);
}

void* _malloc(std::size_t size) {
    return CrtMallocRetry(size);
}

void* _calloc(std::size_t count, std::size_t size) {
    return __calloc_dbg(count, size);
}

long __ftol(double value) {
    return static_cast<long>(value);
}

long long __allshr(long long value, unsigned shift) {
    return shift >= 64 ? (value < 0 ? -1LL : 0LL) : (value >> shift);
}

long long __allmul(long long lhs, long long rhs) {
    return lhs * rhs;
}

unsigned long long __allshl(unsigned long long value, unsigned shift) {
    return shift >= 64 ? 0ULL : (value << shift);
}

int _strncmp(const char* lhs, const char* rhs, std::size_t count) {
    if (count == 0 || lhs == rhs) {
        return 0;
    }
    if (lhs == nullptr) {
        return -1;
    }
    if (rhs == nullptr) {
        return 1;
    }
    return std::strncmp(lhs, rhs, count);
}

char* _strrchr(char* text, int character) {
    return const_cast<char*>(_strrchr(static_cast<const char*>(text), character));
}

const char* _strrchr(const char* text, int character) {
    return text == nullptr ? nullptr : std::strrchr(text, character);
}

long _labs(long value) {
    return value < 0 ? -value : value;
}

char* __ultoa(unsigned long value, char* buffer, int radix) {
    return xtoa(value, buffer, static_cast<unsigned>(radix));
}

char* __ui64toa(unsigned long long value, char* buffer, int radix) {
    return x64toa(value, buffer, static_cast<unsigned>(radix));
}

int _fgetpos(FILE* stream, fpos_t* position) {
    return stream != nullptr && position != nullptr ? std::fgetpos(stream, position) : -1;
}

void __amsg_exit(int message_id) {
    __FF_MSGBANNER();
    CrtPrintRuntimeMessageBanner(message_id);
    __exit(0xff);
}

CrtNewHandler _set_new_handler(CrtNewHandler handler) {
    CrtNewHandler previous = g_new_handler;
    g_new_handler = handler;
    return previous;
}

int __callnewh(std::size_t size) {
    return g_new_handler != nullptr ? g_new_handler(size) : 0;
}

int __heapset(unsigned fill) {
    (void)fill;
    return FinishHeapValidate();
}

void __setdefaultprecision() {
    __controlfp(0x10000U, 0x30000U);
}

int __ms_p5_mp_test_fdiv() {
    return LegacyFdivBugProbeFallback() ? 1 : 0;
}

int __positive(const double* value) {
    return value != nullptr && *value >= 0.0 ? 1 : 0;
}

void __fassign(int negative, char* destination, const char* digits) {
    if (destination == nullptr) {
        return;
    }
    char* out = destination;
    if (negative != 0) {
        *out++ = '-';
    }
    std::strcpy(out, digits != nullptr ? digits : "");
}

int __cfltcvt(const double* value, char* buffer, std::size_t buffer_chars,
    int format, int precision, int uppercase) {
    if (buffer == nullptr || buffer_chars == 0) {
        return -1;
    }
    const double number = value != nullptr ? *value : 0.0;
    const char specifier = format == 0 ? (uppercase ? 'E' : 'e')
        : format == 1 ? 'f'
        : (uppercase ? 'G' : 'g');
    const char format_text[] = {'%', '.', '*', specifier, '\0'};
    return std::snprintf(buffer, buffer_chars, format_text, precision, number);
}

void __shift(char* text, int right) {
    if (text == nullptr || right == 0) {
        return;
    }
    const std::size_t length = std::strlen(text);
    if (right > 0) {
        const auto count = static_cast<std::size_t>(right);
        std::memmove(text + count, text, length + 1);
        std::memset(text, '0', count);
        return;
    }
    const auto count = static_cast<std::size_t>(-right);
    if (count >= length) {
        text[0] = '\0';
        return;
    }
    std::memmove(text, text + count, length - count + 1);
}

double __fload_withFB(double value, int) {
    return value;
}

double __math_exit() {
    return 0.0;
}

double __startOneArgErrorHandling(double value) {
    return value;
}

void __unlock_fhandle(int file_descriptor) {
    (void)file_descriptor;
}

char* __strcats(char* destination, int count, ...) {
    if (destination == nullptr || count <= 0) {
        return destination;
    }
    va_list args;
    va_start(args, count);
    for (int index = 0; index < count; ++index) {
        const char* text = va_arg(args, const char*);
        if (text != nullptr) {
            std::strcat(destination, text);
        }
    }
    va_end(args);
    return destination;
}

void __FF_MSGBANNER() {
    std::fputs("Microsoft Visual C++ Runtime Library\n", stderr);
}

const char* __GET_RTERRMSG(int message_id) {
    for (const RuntimeErrorMessage& message : kRuntimeErrorMessages) {
        if (message.id == message_id) {
            return message.text;
        }
    }
    return "runtime error";
}

void write_multi_char(int character, int count, FILE* stream, void*) {
    FILE* out = stream != nullptr ? stream : stdout;
    for (int index = 0; index < count; ++index) {
        std::fputc(character, out);
    }
}

char* __strrev(char* text) {
    if (text == nullptr) {
        return nullptr;
    }
    std::reverse(text, text + std::strlen(text));
    return text;
}

double __copysign(double value, double sign) {
    return std::copysign(value, sign);
}

double __chgsign(double value) {
    return -value;
}

int __isatty(int file_descriptor) {
#ifdef _WIN32
    return _isatty(file_descriptor);
#else
    (void)file_descriptor;
    return 0;
#endif
}

unsigned __controlfp(unsigned new_value, unsigned mask) {
    return CrtControlFpMasked(new_value, mask & 0xfff7ffffU);
}

void __CopyMan(CrtMantissa96& destination, const CrtMantissa96& source) {
    destination = source;
}

void __FillZeroMan(CrtMantissa96& value) {
    value.words = {};
}

int __IsZeroMan(const CrtMantissa96& value) {
    return std::all_of(value.words.begin(), value.words.end(),
        [](std::uint32_t word) { return word == 0; }) ? 1 : 0;
}

void __fptrap() {
}

int __matherr(void* exception_record) {
    return __umatherr(exception_record);
}

int _ValidateExecute(void* callback) {
#ifdef _WIN32
    return callback != nullptr &&
        IsBadCodePtr(reinterpret_cast<FARPROC>(callback)) == FALSE ? 1 : 0;
#else
    return callback != nullptr ? 1 : 0;
#endif
}

int __ismbbkana(unsigned character) {
    const unsigned byte = character & 0xffu;
    return byte >= 0xa1u && byte <= 0xdfu ? 1 : 0;
}

unsigned ___addl(unsigned left, unsigned right, unsigned* out) {
    const unsigned sum = left + right;
    if (out != nullptr) {
        *out = sum;
    }
    return (sum < left || sum < right) ? 1u : 0u;
}

void EhVectorConstructorIterator(void* first, std::size_t element_size,
    int element_count, EhObjectCallback constructor, EhObjectCallback destructor) {
    auto* cursor = static_cast<unsigned char*>(first);
    int constructed = 0;
    try {
        for (; constructed < element_count; ++constructed, cursor += element_size) {
            if (constructor != nullptr) {
                constructor(cursor);
            }
        }
    } catch (...) {
        if (destructor != nullptr) {
            EhVectorDestructorRange(first, element_size, constructed, destructor);
        }
        throw;
    }
}

void EhVectorDestructorIterator(void* first, std::size_t element_size,
    int element_count, EhObjectCallback destructor) {
    EhVectorDestructorRange(first, element_size, element_count, destructor);
}

void EhVectorDestructorRange(void* first, std::size_t element_size,
    int element_count, EhObjectCallback destructor) {
    if (first == nullptr || destructor == nullptr || element_count <= 0) {
        return;
    }
    auto* cursor = static_cast<unsigned char*>(first) + element_size * (element_count - 1);
    for (int index = element_count - 1; index >= 0; --index, cursor -= element_size) {
        destructor(cursor);
    }
}

void FinishEhVectorConstructorOnException(void* first,
    std::size_t element_size, int constructed_count,
    EhObjectCallback destructor) {
    EhVectorDestructorRange(first, element_size, constructed_count, destructor);
}

void FinishEhVectorConstructor() {
}

void FinishEhVectorDestructorOnException(void* first,
    std::size_t element_size, int element_count, EhObjectCallback destructor) {
    EhVectorDestructorRange(first, element_size, element_count, destructor);
}

void FinishEhVectorDestructor() {
}

void CxxContinueAfterCatchUnwind(EhContinuationCallback continuation) {
    if (continuation != nullptr) {
        continuation();
    }
}

void _CallMemberFunction0(void* object, EhObjectCallback callback) {
    if (callback != nullptr) {
        callback(object);
    }
}

void CallCatchCopyFunction(void* destination, const void* source,
    EhCatchCopyCallback callback) {
    if (callback != nullptr) {
        callback(destination, source);
    }
}

void CallCatchCopyFunctionIndirect(void* destination, const void* source,
    EhCatchCopyCallback* callback) {
    if (callback != nullptr && *callback != nullptr) {
        (*callback)(destination, source);
    }
}

void CxxUnwindFrameAndRestore(void*, void*) {
}

int CallCxxCatchBlockWithFrame(EhContinuationCallback handler,
    void*, void*, void*, int) {
    if (handler != nullptr) {
        handler();
    }
    return 0;
}

int CxxCatchFrameHandler(void* exception_record, void* frame,
    void* dispatcher_context) {
    return CxxFrameHandler(exception_record, frame, nullptr, dispatcher_context,
        nullptr, 0, nullptr, false);
}

int CallCxxExceptionTranslator(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    int catch_depth, void* nested_registration) {
    TranslateAndDispatchSehException(exception_record, registration, context,
        dispatcher_context, function_info, -1, catch_depth, nested_registration);
    return 0;
}

int CxxExceptionTranslatorFilter(void*, void*, void*) {
    return 1;
}

void* FindCxxTryBlockRange(void*, int, int, unsigned* first, unsigned* last) {
    if (first != nullptr) {
        *first = 0;
    }
    if (last != nullptr) {
        *last = 0;
    }
    return nullptr;
}

void __global_unwind2(void*) {
}

int __abnormal_termination() {
    return 0;
}

void __NLG_Notify1(std::uintptr_t) {
}

void NotifyLocalUnwind(std::uintptr_t) {
}

std::size_t _strlen(const char* text) {
    return text == nullptr ? 0 : std::strlen(text);
}

void* _memset(void* destination, int value, std::size_t size) {
    return std::memset(destination, value, size);
}

char* _asctime(const std::tm* value) {
    return value == nullptr ? nullptr : std::asctime(value);
}

char* _strncpy(char* destination, const char* source, std::size_t count) {
    return std::strncpy(destination, source, count);
}

int AsciiToInt(const char* text) {
    return CrtAtoi(text);
}

int _strcmp(const char* lhs, const char* rhs) {
    return std::strcmp(lhs, rhs);
}

int _memcmp(const void* lhs, const void* rhs, std::size_t count) {
    return std::memcmp(lhs, rhs, count);
}

char* _strncat(char* destination, const char* source, std::size_t count) {
    return std::strncat(destination, source, count);
}

std::tm* _gmtime(const std::time_t* value) {
    return value == nullptr ? nullptr : std::gmtime(value);
}

void* __calloc_dbg(std::size_t count, std::size_t size) {
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        return nullptr;
    }
    const std::size_t total = count * size;
    void* memory = CrtDebugHeapAlloc(total);
    if (memory != nullptr) {
        std::memset(memory, 0, total);
    }
    return memory;
}

void* __malloc_dbg(std::size_t size, int block_type, const char* file_name,
    int line_number) {
    return CrtDebugHeapAllocTracked(size, block_type, file_name, line_number);
}

void* __realloc_dbg(void* memory, std::size_t size) {
    return CrtRealloc(memory, size);
}

void __free_dbg(void* memory) {
    CrtDebugHeapFree(memory);
}

bool __CrtIsMemoryBlock(const void* memory, std::size_t size,
    long* request_number, const char** file_name, int* line_number) {
    auto* writable = const_cast<void*>(memory);
    if (writable == nullptr) {
        return false;
    }
    CrtRuntimeLockScope lock(9);
    const auto it = g_debug_allocations.find(writable);
    if (it == g_debug_allocations.end()) {
        return false;
    }
    const CrtDebugAllocation& allocation = it->second;
    const int block_type = allocation.block_type & 0xffff;
    if ((block_type != 1 && block_type != 2 && block_type != 3 && block_type != 4) ||
        allocation.size != size || !ValidateWritePointer(writable, size)) {
        return false;
    }
    if (request_number != nullptr) {
        *request_number = static_cast<long>(allocation.sequence);
    }
    if (file_name != nullptr) {
        *file_name = allocation.file_name;
    }
    if (line_number != nullptr) {
        *line_number = allocation.line_number;
    }
    return true;
}

int __CrtIsValidPointer(const void* memory, std::size_t bytes,
    int write_access) {
    return write_access != 0
        ? ValidateWritePointer(const_cast<void*>(memory), bytes)
        : ValidateReadPointer(memory, bytes);
}

void __local_unwind2(void*, int) {
}

int __futime(int file_descriptor, const std::time_t* access_time,
    const std::time_t* modify_time) {
#ifdef _WIN32
    _utimbuf times{};
    const std::time_t now = std::time(nullptr);
    times.actime = access_time == nullptr ? now : *access_time;
    times.modtime = modify_time == nullptr ? times.actime : *modify_time;
    return _futime(file_descriptor, &times);
#else
    (void)file_descriptor;
    (void)access_time;
    (void)modify_time;
    return 0;
#endif
}

void __dosmaperr(unsigned long error) {
    *CrtDosErrnoPointer() = error;
#ifdef _WIN32
    *CrtErrnoPointer() = errno_from_dos_error(error);
#else
    *CrtErrnoPointer() = EINVAL;
#endif
}

unsigned getSystemCP() {
#ifdef _WIN32
    return GetACP();
#else
    return 0;
#endif
}

int setSBCS() {
    return 0;
}

char* xtoa(unsigned long value, char* buffer, unsigned radix, bool negative) {
    if (buffer == nullptr || radix < 2 || radix > 36) {
        return buffer;
    }
    char temp[65]{};
    char* cursor = temp + sizeof(temp) - 1;
    do {
        const unsigned digit = value % radix;
        *--cursor = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= radix;
    } while (value != 0);
    if (negative) {
        *--cursor = '-';
    }
    std::strcpy(buffer, cursor);
    return buffer;
}

char* x64toa(unsigned long long value, char* buffer, unsigned radix, bool negative) {
    if (buffer == nullptr || radix < 2 || radix > 36) {
        return buffer;
    }
    char temp[65]{};
    char* cursor = temp + sizeof(temp) - 1;
    do {
        const unsigned digit = static_cast<unsigned>(value % radix);
        *--cursor = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= radix;
    } while (value != 0);
    if (negative) {
        *--cursor = '-';
    }
    std::strcpy(buffer, cursor);
    return buffer;
}

char* __strdup(const char* text) {
    if (text == nullptr) {
        return nullptr;
    }
    const std::size_t bytes = std::strlen(text) + 1;
    auto* copy = static_cast<char*>(CrtMallocRetry(bytes));
    if (copy != nullptr) {
        std::memcpy(copy, text, bytes);
    }
    return copy;
}

char* __itoa(int value, char* buffer, int radix) {
    const bool negative = radix == 10 && value < 0;
    const auto magnitude = negative
        ? 0UL - static_cast<unsigned long>(value)
        : static_cast<unsigned long>(value);
    return xtoa(magnitude, buffer, static_cast<unsigned>(radix), negative);
}

char* __ltoa(long value, char* buffer, int radix) {
    const bool negative = radix == 10 && value < 0;
    const auto magnitude = negative
        ? 0UL - static_cast<unsigned long>(value)
        : static_cast<unsigned long>(value);
    return xtoa(magnitude, buffer, static_cast<unsigned>(radix), negative);
}

char* __i64toa(long long value, char* buffer, int radix) {
    const bool negative = radix == 10 && value < 0;
    const auto magnitude = negative
        ? 0ULL - static_cast<unsigned long long>(value)
        : static_cast<unsigned long long>(value);
    return x64toa(magnitude, buffer, static_cast<unsigned>(radix), negative);
}

int __close_lk(int file_descriptor) {
    return CrtCloseFileDescriptor(file_descriptor);
}

void* __malloc_base(std::size_t size) {
    return CrtMallocRetry(size);
}

void* __nh_malloc_base(std::size_t size) {
    return CrtMallocRetry(size);
}

void* __heap_alloc_base(std::size_t size) {
    return CrtMallocRetry(size);
}

int __heapchk() {
    return FinishHeapValidate();
}

int ___sbh_heap_init() {
    return InitializeCrtHeap(true) ? 0 : -1;
}

void __cropzeros(char* text) {
    if (text == nullptr) {
        return;
    }
    char* exponent = std::strchr(text, 'e');
    if (exponent == nullptr) {
        exponent = std::strchr(text, 'E');
    }
    char* end = exponent != nullptr ? exponent : text + std::strlen(text);
    while (end > text && end[-1] == '0') {
        --end;
    }
    if (end > text && end[-1] == '.') {
        --end;
    }
    if (exponent != nullptr) {
        std::memmove(end, exponent, std::strlen(exponent) + 1);
    } else {
        *end = '\0';
    }
}

int __umatherr(void*) {
    return 0;
}

int __set_osfhnd(int file_descriptor, intptr_t os_handle) {
#ifdef _WIN32
    DWORD id = 0xffffffffu;
    if (file_descriptor == 0) {
        id = STD_INPUT_HANDLE;
    } else if (file_descriptor == 1) {
        id = STD_OUTPUT_HANDLE;
    } else if (file_descriptor == 2) {
        id = STD_ERROR_HANDLE;
    }
    return id != 0xffffffffu &&
        SetStdHandle(id, reinterpret_cast<HANDLE>(os_handle)) != FALSE ? 0 : -1;
#else
    (void)file_descriptor;
    (void)os_handle;
    return -1;
#endif
}

void __ioterm() {
    RunCrtExitTerminators();
}

std::string ___lc_lctostr(unsigned locale_id, unsigned locale_type) {
    return LoadLocaleStringValue(locale_id, locale_type, true);
}

void CatchIt() {
}

void ___DestructExceptionObject(void*) {
}

int __XcptFilter(unsigned code, void* exception_pointers) {
    *CrtExceptionPointerSlot() = exception_pointers;
#ifdef _WIN32
    int signal = 0;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        signal = 11;
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        signal = 4;
        break;
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
        signal = 5;
        break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
        signal = 8;
        *CrtFpecodePointer() = static_cast<int>(code);
        break;
    default:
        break;
    }
    if (signal != 0 && CrtRaiseSignal(signal) == 0) {
        return 1;
    }
#else
    (void)code;
#endif
    return 0;
}

unsigned long long __aulldiv(unsigned long long dividend,
    unsigned long long divisor) {
    return divisor == 0 ? 0 : dividend / divisor;
}

unsigned long long __aullrem(unsigned long long dividend,
    unsigned long long divisor) {
    return divisor == 0 ? 0 : dividend % divisor;
}

char* __getenv_lk(const char* name) {
    return name == nullptr ? nullptr : std::getenv(name);
}

void __IncMan(CrtMantissa96& mantissa) {
    for (std::uint32_t& word : mantissa.words) {
        if (++word != 0) {
            break;
        }
    }
}

void __ShrMan(CrtMantissa96& mantissa) {
    mantissa.words[0] = (mantissa.words[0] >> 1) | (mantissa.words[1] << 31);
    mantissa.words[1] = (mantissa.words[1] >> 1) | (mantissa.words[2] << 31);
    mantissa.words[2] >>= 1;
}

int ___init_time() {
    RebuildCrtTimeZoneFromEnvironment();
    return 0;
}

void ___free_lc_time() {
}

std::string fix_grouping(const char* grouping) {
    return grouping != nullptr ? std::string(grouping) : std::string();
}

int crtGetLocaleInfoA(unsigned locale_id, unsigned locale_type, char* buffer,
    int buffer_chars) {
    return GetLocaleInfoAnsiCompat(locale_id, locale_type, buffer, buffer_chars);
}

std::size_t _GetPrimaryLen(const char* text) {
    return text == nullptr ? 0 : std::strlen(text);
}

void ___add_12(CrtMantissa96& lhs, const CrtMantissa96& rhs) {
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < lhs.words.size(); ++i) {
        const std::uint64_t sum =
            static_cast<std::uint64_t>(lhs.words[i]) + rhs.words[i] + carry;
        lhs.words[i] = static_cast<std::uint32_t>(sum);
        carry = sum >> 32;
    }
}

void ___shl_12(CrtMantissa96& value, unsigned count) {
    while (count-- != 0) {
        value.words[2] = (value.words[2] << 1) | (value.words[1] >> 31);
        value.words[1] = (value.words[1] << 1) | (value.words[0] >> 31);
        value.words[0] <<= 1;
    }
}

void ___shr_12(CrtMantissa96& value, unsigned count) {
    while (count-- != 0) {
        __ShrMan(value);
    }
}

char* findenv(const char* name) {
    return name == nullptr ? nullptr : std::getenv(name);
}

std::vector<std::string> copy_environ() {
    return InitializeEnvironmentVector();
}

int __mbscoll(const unsigned char* left, const unsigned char* right) {
    const auto* lhs = reinterpret_cast<const char*>(left == nullptr
        ? reinterpret_cast<const unsigned char*>("") : left);
    const auto* rhs = reinterpret_cast<const char*>(right == nullptr
        ? reinterpret_cast<const unsigned char*>("") : right);
    return std::strcmp(lhs, rhs);
}

int __mbsicoll(const unsigned char* left, const unsigned char* right) {
    const auto* lhs = reinterpret_cast<const char*>(left == nullptr
        ? reinterpret_cast<const unsigned char*>("") : left);
    const auto* rhs = reinterpret_cast<const char*>(right == nullptr
        ? reinterpret_cast<const unsigned char*>("") : right);
    while (*lhs != '\0' && *rhs != '\0') {
        const int diff = std::tolower(static_cast<unsigned char>(*lhs)) -
            std::tolower(static_cast<unsigned char>(*rhs));
        if (diff != 0) {
            return diff;
        }
        ++lhs;
        ++rhs;
    }
    return static_cast<unsigned char>(*lhs) - static_cast<unsigned char>(*rhs);
}

int __mbsnbicoll(const unsigned char* left, const unsigned char* right,
    std::size_t count) {
    const auto* lhs = reinterpret_cast<const char*>(left == nullptr
        ? reinterpret_cast<const unsigned char*>("") : left);
    const auto* rhs = reinterpret_cast<const char*>(right == nullptr
        ? reinterpret_cast<const unsigned char*>("") : right);
    for (std::size_t i = 0; i < count; ++i) {
        const int lc = std::tolower(static_cast<unsigned char>(lhs[i]));
        const int rc = std::tolower(static_cast<unsigned char>(rhs[i]));
        if (lc != rc || lc == '\0' || rc == '\0') {
            return lc - rc;
        }
    }
    return 0;
}

char* __get_fname(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* drive = std::strrchr(path, ':');
    const char* sep = std::max({slash, backslash, drive});
    return const_cast<char*>(sep == nullptr ? path : sep + 1);
}

} // namespace ranker
