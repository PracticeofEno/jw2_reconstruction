#include "ranker_startup_environment.h"

#include "ranker_win32_compat.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <vector>

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

} // namespace ranker
