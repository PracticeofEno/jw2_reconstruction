#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>
#include <stdexcept>

namespace ranker {

bool GetSimpleExceptionErrorMessage(MfcSimpleExceptionCompat& exception,
    char* destination, int destination_chars, unsigned* help_context) {
    if (!ValidateAnsiStringPointer(destination,
        destination_chars > 0 ? static_cast<std::size_t>(destination_chars) : 0)) {
        CrtDbgReport(2, "except.cpp", 0x124, nullptr,
            "invalid CSimpleException destination buffer");
        return false;
    }
    if (help_context != nullptr) {
        *help_context = 0;
    }
    if (!exception.string_initialized) {
        exception.string_initialized = true;
        exception.has_message = exception.message[0] != '\0';
    }
    if (!exception.has_message) {
        destination[0] = '\0';
    } else {
#ifdef _WIN32
        lstrcpynA(destination, exception.message.data(), destination_chars);
#else
        std::strncpy(destination, exception.message.data(),
            static_cast<std::size_t>(destination_chars));
        destination[destination_chars - 1] = '\0';
#endif
    }
    return exception.has_message;
}

[[noreturn]] void ThrowMfcMemoryException() {
    throw std::bad_alloc();
}

[[noreturn]] void ThrowMfcResourceException() {
    throw std::runtime_error("MFC resource exception");
}

MfcSimpleExceptionCompat& ConstructSimpleException(
    MfcSimpleExceptionCompat& exception, bool auto_delete,
    unsigned help_context) {
    exception = MfcSimpleExceptionCompat{};
    exception.auto_delete = auto_delete;
    exception.help_context = help_context;
    return exception;
}

void DestroyExceptionBase(MfcSimpleExceptionCompat& exception) {
    DestroySimpleException(exception);
}

void DestroySimpleException(MfcSimpleExceptionCompat& exception) {
    exception = MfcSimpleExceptionCompat{};
}

MfcSimpleExceptionCompat* DeleteSimpleExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroySimpleException(*exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

MfcSimpleExceptionCompat& ConstructMemoryException(
    MfcSimpleExceptionCompat& exception) {
    ConstructSimpleException(exception);
    std::strncpy(exception.message.data(), "memory exception",
        exception.message.size() - 1);
    exception.has_message = true;
    exception.string_initialized = true;
    return exception;
}

void DestroyMemoryException(MfcSimpleExceptionCompat& exception) {
    DestroySimpleException(exception);
}

MfcSimpleExceptionCompat* DeleteMemoryExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroyMemoryException(*exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

MfcSimpleExceptionCompat& ConstructNotSupportedException(
    MfcSimpleExceptionCompat& exception) {
    ConstructSimpleException(exception);
    std::strncpy(exception.message.data(), "operation not supported",
        exception.message.size() - 1);
    exception.has_message = true;
    exception.string_initialized = true;
    return exception;
}

void DestroyNotSupportedException(MfcSimpleExceptionCompat& exception) {
    DestroySimpleException(exception);
}

MfcSimpleExceptionCompat* DeleteNotSupportedExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroyNotSupportedException(*exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

MfcSimpleExceptionCompat& ConstructUserException(
    MfcSimpleExceptionCompat& exception, unsigned help_context,
    const char* message) {
    ConstructSimpleException(exception, false, help_context);
    if (message != nullptr) {
        std::strncpy(exception.message.data(), message,
            exception.message.size() - 1);
        exception.has_message = exception.message[0] != '\0';
        exception.string_initialized = true;
    }
    return exception;
}

void DestroyUserException(MfcSimpleExceptionCompat& exception) {
    DestroySimpleException(exception);
}

MfcSimpleExceptionCompat* DeleteUserExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroyUserException(*exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

MfcFileExceptionCompat& ConstructFileException(
    MfcFileExceptionCompat& exception, unsigned cause, long os_error,
    const char* file_name) {
    exception = MfcFileExceptionCompat{};
    exception.cause = cause;
    exception.os_error = os_error;
    exception.file_name = file_name == nullptr ? "" : file_name;
    return exception;
}

void DestroyFileException(MfcFileExceptionCompat& exception) {
    exception = MfcFileExceptionCompat{};
}

MfcFileExceptionCompat* DeleteFileExceptionScalarDtor(
    MfcFileExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroyFileException(*exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

bool SimpleExceptionGetAutoDelete(const MfcSimpleExceptionCompat& exception) {
    return exception.auto_delete;
}

bool GetFileExceptionErrorMessage(const MfcFileExceptionCompat& exception,
    char* destination, int destination_chars, unsigned* help_context) {
    if (help_context != nullptr) {
        *help_context = 0;
    }
    if (destination == nullptr || destination_chars <= 0) {
        return false;
    }

    static constexpr const char* kCauseNames[] = {
        "none", "generic", "file not found", "bad path",
        "too many open files", "access denied", "invalid file",
        "remove current directory", "directory full", "bad seek",
        "hard IO error", "sharing violation", "lock violation",
        "disk full", "end of file",
    };
    const char* cause = exception.cause < std::size(kCauseNames)
        ? kCauseNames[exception.cause] : "unknown";
    const char* file = exception.file_name.empty()
        ? "Unknown" : exception.file_name.c_str();
    std::snprintf(destination, static_cast<std::size_t>(destination_chars),
        "File exception: %s. File: %s. OS error: %ld.",
        cause, file, exception.os_error);
    destination[destination_chars - 1] = '\0';
    return true;
}

[[noreturn]] void ThrowFileException(unsigned cause, long os_error,
    const char* file_name) {
    MfcFileExceptionCompat exception{};
    exception.cause = cause;
    exception.os_error = os_error;
    exception.file_name = file_name == nullptr ? "" : file_name;

    char message[256]{};
    GetFileExceptionErrorMessage(exception, message, sizeof(message), nullptr);
    AfxTraceOutput("CFile exception: %s\n", message);
    throw std::runtime_error(message);
}

unsigned FileExceptionCauseFromErrno(int value) {
    switch (value) {
    case 1:
    case 13:
        return 5;
    case 2:
    case 0x17:
        return 2;
    case 5:
    case 0x16:
        return 10;
    case 9:
        return 6;
    case 0x18:
        return 4;
    case 0x1c:
        return 13;
    case 0x24:
        return 11;
    default:
        return 1;
    }
}

unsigned FileExceptionCauseFromOsError(unsigned long value) {
    switch (value) {
    case ERROR_SUCCESS:
        return 0;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return 2;
    case ERROR_ACCESS_DENIED:
    case ERROR_CURRENT_DIRECTORY:
    case ERROR_WRITE_PROTECT:
    case ERROR_NETWORK_ACCESS_DENIED:
        return 5;
    case ERROR_INVALID_HANDLE:
    case ERROR_INVALID_PARAMETER:
        return 6;
    case ERROR_TOO_MANY_OPEN_FILES:
        return 4;
    case ERROR_SEEK:
    case ERROR_NEGATIVE_SEEK:
    case ERROR_SEEK_ON_DEVICE:
        return 9;
    case ERROR_WRITE_FAULT:
    case ERROR_READ_FAULT:
    case ERROR_GEN_FAILURE:
        return 10;
    case ERROR_SHARING_VIOLATION:
        return 11;
    case ERROR_LOCK_VIOLATION:
        return 12;
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_FULL:
        return 13;
    default:
        return 1;
    }
}

} // namespace ranker
