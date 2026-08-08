#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"

#include <cstdlib>

namespace ranker {

namespace {

using MfcNewHandler = int (*)(std::size_t size);

bool g_mfc_memory_tracking_enabled = true;
MfcNewHandler g_mfc_new_handler = nullptr;

} // namespace

void* MfcDebugNewClientBlock(std::size_t size) {
    return CrtDebugHeapAlloc(size);
}

void MfcDebugDeleteClientBlock(void* memory) {
    CrtDebugHeapFree(memory);
}

void MfcDebugDeleteClientBlockAlias(void* memory) {
    MfcDebugDeleteClientBlock(memory);
}

void MfcDebugDeleteNormalBlock(void* memory) {
    CrtDebugHeapFree(memory);
}

void MfcDebugDeleteNormalBlockThunk(void* memory) {
    MfcDebugDeleteNormalBlock(memory);
}

void* MfcDebugNewClientBlockWithFile(std::size_t size, const char*, int) {
    return CrtDebugHeapAlloc(size);
}

void* operator_new(std::size_t size, const char* file, int line) {
    return MfcDebugNewClientBlockWithFile(size, file, line);
}

void MfcDebugDeleteClientBlockWithFile(void* memory) {
    MfcDebugDeleteClientBlock(memory);
}

void* MfcDebugMallocByBlockUse(std::size_t size, int, const char*, int) {
    return CrtDebugHeapAlloc(size);
}

void MfcDebugFreeByBlockUse(void* memory, int) {
    CrtDebugHeapFree(memory);
}

int AfxAllocHookAlwaysAllow() {
    return 1;
}

int AfxAllocHookThunk(int, void*, std::size_t, int, const char*, int) {
    return AfxAllocHookAlwaysAllow();
}

int ArrayUnwindFilter(_EXCEPTION_POINTERS*) {
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void _abort() {
    std::abort();
}

bool AfxEnableMemoryTracking(bool enabled) {
    const bool previous = g_mfc_memory_tracking_enabled;
    g_mfc_memory_tracking_enabled = enabled;
    const int flags = CrtSetDebugFlag(-1);
    CrtSetDebugFlag(enabled ? (flags | 1) : (flags & ~1));
    return previous;
}

bool AfxCheckMemoryCompat() {
    return CrtCheckMemory();
}

void MfcMemoryStateDumpStatistics(const CrtMemState& state) {
    CrtMemDumpStatistics(state);
}

void MfcMemoryStateDumpAllObjectsSince(const CrtMemState* state) {
    CrtDumpAllObjectsSince(state);
}

void AfxDoForAllObjectsCompat(CrtClientObjectCallback callback, void* context) {
    CrtDoForAllClientObjects(callback, context);
}

bool AfxDumpMemoryLeaksCompat() {
    return CrtDumpMemoryLeaks();
}

int ThrowMfcMemoryExceptionAsInt() {
    ThrowMfcMemoryException();
}

MfcNewHandler AfxGetNewHandlerCompat() {
    return g_mfc_new_handler;
}

MfcNewHandler AfxSetNewHandlerCompat(MfcNewHandler handler) {
    MfcNewHandler previous = g_mfc_new_handler;
    g_mfc_new_handler = handler;
    return previous;
}

void MfcExceptionDestroyStorage(void* exception_storage, bool auto_delete) {
    if (auto_delete) {
        MfcDebugDeleteClientBlock(exception_storage);
    }
}

void MfcExceptionDestroyStorageAlias(void* exception_storage) {
    MfcExceptionDestroyStorage(exception_storage, true);
}

int MfcExceptionReportError(MfcSimpleExceptionCompat& exception,
    unsigned message_box_flags, unsigned default_help_id) {
    char message[512]{};
    unsigned help_id = default_help_id;
    if (GetSimpleExceptionErrorMessage(exception, message,
        static_cast<int>(sizeof(message)), &help_id)) {
#ifdef _WIN32
        return MessageBoxA(nullptr, message, "Ranker", message_box_flags);
#else
        (void)message_box_flags;
        AfxTraceOutput("%s\n", message);
        return 0;
#endif
    }
    return 0;
}

void AfxExceptionLinkUnlink() {
    AfxExceptionContextCompat& context = AfxExceptionContextCompatState();
    AfxExceptionLinkCompat* link = context.current;
    if (link == nullptr) {
        if (CrtDbgReport(2, "except.cpp", 0x8e, nullptr, nullptr) == 1) {
            CrtDebugBreak();
        }
        return;
    }
    context.current = link->previous;
    link->previous = nullptr;
    link->exception = nullptr;
}

MfcSimpleExceptionCompat* DeleteMfcExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    exception->has_message = false;
    exception->message = {};
    if ((flags & 1U) != 0U) {
        delete exception;
    }
    return exception;
}

} // namespace ranker
