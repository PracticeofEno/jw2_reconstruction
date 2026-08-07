#include "ranker_startup_environment.h"

#include "ranker_crt_runtime.h"
#include "ranker_legacy_environment.h"
#include "ranker_text_tables.h"
#include "ranker_win32_compat.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#endif

namespace ranker {
namespace {

#ifdef _WIN32
LONG CALLBACK log_startup_vectored_exception(EXCEPTION_POINTERS* pointers) {
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    if (record.ExceptionCode == DBG_PRINTEXCEPTION_C ||
        record.ExceptionCode == 0x4001000aUL ||
        record.ExceptionCode == 0x406d1388UL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void* ip = nullptr;
    if (pointers->ContextRecord != nullptr) {
#if defined(_M_IX86)
        ip = reinterpret_cast<void*>(pointers->ContextRecord->Eip);
#elif defined(_M_X64)
        ip = reinterpret_cast<void*>(pointers->ContextRecord->Rip);
#elif defined(__i386__)
        ip = reinterpret_cast<void*>(pointers->ContextRecord->Eip);
#elif defined(__x86_64__)
        ip = reinterpret_cast<void*>(pointers->ContextRecord->Rip);
#endif
    }

    const ULONG_PTR info0 =
        record.NumberParameters > 0 ? record.ExceptionInformation[0] : 0;
    const ULONG_PTR info1 =
        record.NumberParameters > 1 ? record.ExceptionInformation[1] : 0;
    const void* image_base = GetModuleHandleA(nullptr);
    append_startup_log(
        "SEH code=0x%08lx flags=0x%08lx addr=%p ip=%p image_base=%p params=%lu info0=%p info1=%p",
        static_cast<unsigned long>(record.ExceptionCode),
        static_cast<unsigned long>(record.ExceptionFlags),
        record.ExceptionAddress, ip, image_base,
        static_cast<unsigned long>(record.NumberParameters),
        reinterpret_cast<void*>(info0), reinterpret_cast<void*>(info1));
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

bool has_ranker_data_directory(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::is_regular_file(directory / "JW2_01.TRC", ec) &&
        fs::is_regular_file(directory / "Jw2_08.trc", ec) &&
        fs::is_regular_file(directory / "Jw2_17.trc", ec);
}

void add_data_directory_candidates(std::vector<std::filesystem::path>& candidates,
    const std::filesystem::path& base) {
    if (base.empty()) {
        return;
    }

    candidates.push_back(base);
    candidates.push_back(base / "RankerOCPV_Win");
    candidates.push_back(base.parent_path() / "RankerOCPV_Win");
    candidates.push_back(base.parent_path().parent_path() / "RankerOCPV_Win");
}

#ifdef _WIN32
constexpr u64 kExpectedJw208Size = 0x0ddcc154ULL;
constexpr u32 kSetupVersionXor = 0x11223344u;
constexpr std::size_t kSetupRegistrySubkeyTextRow = 224;
constexpr const char* kSetupRegistrySubkeyFallback =
    "Software\\WIZARD SOFT\\The Ranker";

bool query_registry_value_bytes(HKEY root, const char* subkey,
    const char* value_name, DWORD* type, BYTE* data, DWORD* byte_count) {
    HKEY key = nullptr;
    const LSTATUS open_result =
        RegOpenKeyExA(root, subkey, 0, KEY_QUERY_VALUE, &key);
    if (open_result != ERROR_SUCCESS) {
        return false;
    }

    const LSTATUS query_result = RegQueryValueExA(
        key, value_name, nullptr, type, data, byte_count);
    RegCloseKey(key);
    return query_result == ERROR_SUCCESS;
}

bool read_setup_version_file_value(u32& value) {
    char windows_directory[MAX_PATH]{};
    if (GetWindowsDirectoryA(windows_directory,
            static_cast<UINT>(sizeof(windows_directory))) == 0) {
        return false;
    }

    std::string path = windows_directory;
    path += "\\setuprk.dat";

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path.c_str(), "r+b") != 0) {
        file = nullptr;
    }
#else
    file = std::fopen(path.c_str(), "r+b");
#endif
    if (file == nullptr) {
        return false;
    }

    u32 stored_value = 0;
    const bool ok = std::fread(&stored_value, sizeof(stored_value), 1, file) == 1;
    std::fclose(file);
    if (!ok) {
        return false;
    }

    value = stored_value ^ kSetupVersionXor;
    return true;
}

bool query_setup_registry_version_value(u32& value) {
    DWORD type = REG_DWORD;
    DWORD byte_count = sizeof(value);
    const char* subkey = startup_platform_row(
        kSetupRegistrySubkeyTextRow, kSetupRegistrySubkeyFallback);
    return query_registry_value_bytes(HKEY_LOCAL_MACHINE, subkey, "VersionData",
               &type, reinterpret_cast<LPBYTE>(&value), &byte_count) &&
        byte_count == sizeof(value);
}

bool find_expected_jw208_archive_on_cdrom() {
    if (!InitializeCdRomDriveScan()) {
        return false;
    }

    while (!legacy_environment_state().selected_cdrom_root.empty()) {
        std::string path = legacy_environment_state().selected_cdrom_root;
        path += "jw2_08.trc";

        CrtFindDataA find_data{};
        const HANDLE find = CrtFindFirstFile(path.c_str(), find_data);
        if (find != INVALID_HANDLE_VALUE) {
            HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                const DWORD size = GetFileSize(file, nullptr);
                CloseHandle(file);
                if (size == kExpectedJw208Size) {
                    CrtFindClose(find);
                    return true;
                }
            }
        }
        SelectNextCdRomDrive();
    }

    return false;
}
#endif

} // namespace

void append_startup_log(const char* format, ...) {
    if (format == nullptr) {
        return;
    }

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, "Jw2.log", "a") != 0) {
        file = nullptr;
    }
#else
    file = std::fopen("Jw2.log", "a");
#endif
    if (file == nullptr) {
        return;
    }

    std::fputs("[rebuild] ", file);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

void install_startup_exception_logger_once() {
#ifdef _WIN32
    static void* handler = nullptr;
    if (handler != nullptr) {
        return;
    }

    handler = AddVectoredExceptionHandler(1, log_startup_vectored_exception);
    append_startup_log("SEH logger install %s", handler != nullptr ? "ok" : "failed");
#endif
}

bool ensure_ranker_data_working_directory() {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;

    std::error_code ec;
    const fs::path current = fs::current_path(ec);
    add_data_directory_candidates(candidates, current);

#ifdef _WIN32
    char module_path_buffer[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, module_path_buffer,
            static_cast<DWORD>(sizeof(module_path_buffer))) != 0) {
        const fs::path module_dir = fs::path(module_path_buffer).parent_path();
        add_data_directory_candidates(candidates, module_dir);
    }
#endif

    for (const fs::path& candidate : candidates) {
        if (!has_ranker_data_directory(candidate)) {
            continue;
        }

        std::error_code set_ec;
        fs::current_path(candidate, set_ec);
        if (!set_ec) {
            append_startup_log("data cwd=%s", candidate.string().c_str());
            return true;
        }
    }

    append_startup_log("data cwd not found");
    return false;
}

bool CpuSupportsMmx() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int registers[4]{};
    __cpuid(registers, 1);
    return (registers[3] & (1 << 23)) != 0;
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) {
        return false;
    }
    return (edx & (1u << 23)) != 0;
#else
    return false;
#endif
}

void WriteStartupTimestampLog(const char* path) {
    if (path == nullptr || *path == '\0') {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "a") != 0) {
        file = nullptr;
    }
#else
    file = std::fopen(path, "a");
#endif
    if (file == nullptr) {
        return;
    }

    if (local != nullptr) {
        std::fputs(std::asctime(local), file);
    }
    std::fclose(file);
}

bool VerifySetupVersionData() {
#ifdef _WIN32
    u32 registry_value = 0;
    u32 file_value = 0;
    return query_setup_registry_version_value(registry_value) &&
        read_setup_version_file_value(file_value) &&
        registry_value == file_value;
#else
    return false;
#endif
}

bool VerifySetupOrFindJw208Archive() {
    if (VerifySetupVersionData()) {
        return true;
    }

#ifdef _WIN32
    return find_expected_jw208_archive_on_cdrom();
#else
    return false;
#endif
}

bool background_test_mode_enabled() {
    const char* value = std::getenv("RANKER_REBUILD_BACKGROUND_TEST");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

} // namespace ranker
