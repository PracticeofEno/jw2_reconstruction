#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"
#include "ranker_win32_compat.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <dde.h>
#endif

namespace ranker {
namespace {

// MFC debug hooks are process-global in the original runtime. Keeping their
// state beside the dump/report implementation prevents unrelated window and
// OLE compatibility code from depending on these internals.
MfcDumpContext g_dump_context;
AfxDebugState g_debug_state;
bool g_debug_state_created = false;

void write_debug_text(const char* text) {
    if (text == nullptr) {
        text = "(NULL)";
    }
#ifdef _WIN32
    OutputDebugStringA(text);
#else
    std::fputs(text, stderr);
#endif
}

AfxDumpClientCallback set_dump_client(AfxDumpClientCallback callback) {
    return CrtSetDumpClient(callback);
}

AfxReportHookCallback set_report_hook(AfxReportHookCallback callback) {
    return CrtSetReportHook(callback);
}

} // namespace

void AfxDumpStaticObjectMessage(int static_object) {
    if (!g_debug_state.trace_enabled) {
        return;
    }
    write_debug_text(static_object == 0
        ? "\n"
        : "Unable to dump object in static region.\n");
}

void AfxTraceOutput(const char* format, ...) {
    if (!g_debug_state.trace_enabled || format == nullptr) {
        return;
    }

    char buffer[512]{};
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written < 0) {
        CrtDbgReport(2, "dumpout.cpp", 0x34, nullptr,
            "AfxTraceOutput formatting failed");
        return;
    }

    write_debug_text(buffer);
}

namespace {

const char* trace_message_name(UINT message) {
    switch (message) {
    case WM_CREATE:
        return "WM_CREATE";
    case WM_DESTROY:
        return "WM_DESTROY";
    case WM_MOVE:
        return "WM_MOVE";
    case WM_SIZE:
        return "WM_SIZE";
    case WM_PAINT:
        return "WM_PAINT";
    case WM_CLOSE:
        return "WM_CLOSE";
    case WM_QUERYENDSESSION:
        return "WM_QUERYENDSESSION";
    case WM_NCCALCSIZE:
        return "WM_NCCALCSIZE";
    case WM_NCPAINT:
        return "WM_NCPAINT";
    case WM_KEYDOWN:
        return "WM_KEYDOWN";
    case WM_KEYUP:
        return "WM_KEYUP";
    case WM_MOUSEMOVE:
        return "WM_MOUSEMOVE";
    case WM_LBUTTONDOWN:
        return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP:
        return "WM_LBUTTONUP";
    case WM_COMMAND:
        return "WM_COMMAND";
    case WM_NOTIFY:
        return "WM_NOTIFY";
    default:
        return nullptr;
    }
}

} // namespace

void AfxTraceDdeMessage(const char* label, const MSG& message) {
    if (message.message == WM_DDE_EXECUTE) {
        UINT_PTR unused = 0;
        HGLOBAL command = nullptr;
        if (UnpackDDElParam(WM_DDE_EXECUTE, message.lParam, &unused,
                reinterpret_cast<PUINT_PTR>(&command)) && command != nullptr) {
            const void* text = GlobalLock(command);
            AfxTraceOutput("%s Execute: %s\n", label, text);
            if (text != nullptr) {
                GlobalUnlock(command);
            }
            return;
        }
    }
    AfxTraceOutput("%s DDE hwnd=%p msg=0x%04x wParam=%p lParam=%p\n",
        label, message.hwnd, message.message,
        reinterpret_cast<void*>(message.wParam),
        reinterpret_cast<void*>(message.lParam));
}

void AfxTraceWindowMessage(const char* label, const MSG& message) {
    if (label == nullptr) {
        label = "Message";
    }
    const char* name = trace_message_name(message.message);
    if (name != nullptr) {
        AfxTraceOutput("%s hwnd=%p msg=%s wParam=%p lParam=%p\n", label,
            message.hwnd, name, reinterpret_cast<void*>(message.wParam),
            reinterpret_cast<void*>(message.lParam));
    } else if (message.message >= WM_USER && message.message < 0xc000) {
        AfxTraceOutput("%s hwnd=%p msg=WM_USER+0x%04x wParam=%p lParam=%p\n",
            label, message.hwnd, message.message - WM_USER,
            reinterpret_cast<void*>(message.wParam),
            reinterpret_cast<void*>(message.lParam));
    } else {
        AfxTraceOutput("%s hwnd=%p msg=0x%04x wParam=%p lParam=%p\n",
            label, message.hwnd, message.message,
            reinterpret_cast<void*>(message.wParam),
            reinterpret_cast<void*>(message.lParam));
    }
    if (message.message > WM_DDE_FIRST && message.message < WM_DDE_LAST) {
        AfxTraceDdeMessage(label, message);
    }
}

void InitializeGlobalDumpContextThunk() {
    InitializeGlobalDumpContext();
}

void InitializeGlobalDumpContext() {
    g_dump_context.depth = 0;
    g_dump_context.file = nullptr;
}

MfcDumpContext& ConstructDumpContext(MfcDumpContext& context, void* file) {
    if (file != nullptr) {
        AfxAssertValidObject(static_cast<const MfcObjectCompat*>(file),
            "dumpcont.cpp", 0x2f);
    }
    context.depth = 0;
    context.file = file;
    return context;
}

int DumpContextGetDepth(const MfcDumpContext& context) {
    return context.depth;
}

void DumpContextSetDepth(MfcDumpContext& context, int depth) {
    context.depth = depth;
}

int DumpContextDepthValue(int depth) {
    return depth;
}

void DumpContextNoopInline() {
}

void DumpContextOutputString(MfcDumpContext& context, const char* text) {
    if (!g_debug_state.trace_enabled) {
        return;
    }
    if (text == nullptr) {
        text = "(NULL)";
    }
    if (context.file != nullptr) {
        auto* file = static_cast<MfcFileCompat*>(context.file);
        FileWrite(*file, text, static_cast<unsigned>(std::strlen(text)));
        return;
    }
    if (CrtDbgReport(0, nullptr, 0, nullptr, "%s", text) == 1) {
        CrtDebugBreak();
    }
}

void DumpContextFlush(MfcDumpContext& context) {
    if (context.file != nullptr) {
        FileFlush(*static_cast<MfcFileCompat*>(context.file));
    }
}

MfcDumpContext& DumpContextWriteString(MfcDumpContext& context,
    const char* text) {
    if (text == nullptr) {
        DumpContextOutputString(context, "(NULL)");
        return context;
    }
    if (!g_debug_state.trace_enabled) {
        return context;
    }
    if (context.file != nullptr) {
        DumpContextOutputString(context, text);
        return context;
    }
    char buffer[512]{};
    char* output = buffer;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (buffer + 0x1fd < output) {
            *output = '\0';
            DumpContextOutputString(context, buffer);
            output = buffer;
        }
        if (*cursor == '\n') {
            *output++ = '\r';
        }
        *output++ = *cursor;
    }
    *output = '\0';
    DumpContextOutputString(context, buffer);
    return context;
}

MfcDumpContext& DumpContextWriteByte(MfcDumpContext& context, unsigned value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02X", value & 0xffU);
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteWord(MfcDumpContext& context, unsigned value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04X", value & 0xffffU);
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteDWord(MfcDumpContext& context, DWORD value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%08lX",
        static_cast<unsigned long>(value));
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteLong(MfcDumpContext& context, long value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteFloat(MfcDumpContext& context, float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteDouble(MfcDumpContext& context, double value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteULong(MfcDumpContext& context,
    unsigned long value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%lu", value);
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWritePointer(MfcDumpContext& context,
    const void* value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "$%08lX",
        static_cast<unsigned long>(
            reinterpret_cast<std::uintptr_t>(value) & 0xffffffffUL));
    return DumpContextWriteString(context, buffer);
}

MfcDumpContext& DumpContextWriteObjectPointer(MfcDumpContext& context,
    const void* object) {
    if (object == nullptr) {
        return DumpContextWriteString(context, "NULL");
    }
    DumpContextWriteString(context,
        "Unable to dump object in static region.\n");
    return context;
}

void DumpContextWriteObjectPointerThunk(MfcDumpContext& context,
    const void* object) {
    DumpContextWriteObjectPointer(context, object);
}

MfcDumpContext& DumpContextWriteHex(MfcDumpContext& context, DWORD value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX",
        static_cast<unsigned long>(value));
    return DumpContextWriteString(context, buffer);
}

void DumpContextDumpBytes(MfcDumpContext& context, const char* prefix_format,
    const unsigned char* data, int count, int per_line) {
    if (count <= 0) {
        if (AfxAssertFailedLine("dumpcont.cpp", 0xc9)) {
            CrtDebugBreak();
        }
        return;
    }
    if (per_line <= 0) {
        if (AfxAssertFailedLine("dumpcont.cpp", 0xca)) {
            CrtDebugBreak();
        }
        return;
    }
    if (prefix_format == nullptr) {
        if (AfxAssertFailedLine("dumpcont.cpp", 0xcb)) {
            CrtDebugBreak();
        }
        return;
    }
    if (data == nullptr) {
        if (AfxAssertFailedLine("dumpcont.cpp", 0xcc)) {
            CrtDebugBreak();
        }
        return;
    }
    int line_count = 0;
    for (int index = 0; index < count; ++index) {
        if (line_count == 0) {
            char prefix[64]{};
            std::snprintf(prefix, sizeof(prefix), prefix_format, data + index);
            DumpContextWriteString(context, prefix);
        }
        char byte_text[8]{};
        std::snprintf(byte_text, sizeof(byte_text), " %02X", data[index]);
        DumpContextWriteString(context, byte_text);
        ++line_count;
        if (line_count >= per_line) {
            DumpContextWriteString(context, "\n");
            line_count = 0;
        }
    }
    if (line_count != 0) {
        DumpContextWriteString(context, "\n");
    }
}

MfcDumpContext& DumpContextWriteWideString(MfcDumpContext& context,
    const wchar_t* text) {
    if (text == nullptr) {
        return DumpContextWriteString(context, "(NULL)");
    }
    char buffer[512]{};
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer,
        static_cast<int>(sizeof(buffer)), nullptr, nullptr);
    return DumpContextWriteString(context, buffer);
}

void InitializeAfxDebugStateThunk() {
    InitializeAfxDebugState();
}

void InitializeAfxDebugState() {
    g_debug_state_created = EnsureAfxDebugState();
}

void AfxDumpClientBridge(const void* object, std::size_t bytes) {
    if (object == nullptr) {
        write_debug_text("an invalid object at $00000000, 0 bytes long\n");
    } else if (g_dump_context.depth < 1) {
        char buffer[256]{};
        std::snprintf(buffer, sizeof(buffer),
            "a CObject object at $%p, %u bytes long\n", object,
            static_cast<unsigned>(bytes));
        write_debug_text(buffer);
    } else {
        write_debug_text("}\n");
    }

    if (g_debug_state.previous_dump_client != nullptr) {
        g_debug_state.previous_dump_client(object, bytes);
    }
}

int AfxReportHookBridge(int report_type, const char* message, int* result) {
    if (g_debug_state.previous_report_hook != nullptr &&
        g_debug_state.previous_report_hook(report_type, message, result) != 0) {
        return 1;
    }

    if (report_type == 2 || !g_debug_state.trace_enabled) {
        return 0;
    }

    if (result != nullptr) {
        *result = 0;
    }
    write_debug_text(message);
    return 1;
}

AfxDebugState* ConstructAfxDebugState(AfxDebugState* state) {
    if (state == nullptr) {
        return nullptr;
    }
    ConstructMfcNoTrackObjectBase(state);
#ifdef _WIN32
    state->trace_enabled =
        GetPrivateProfileIntA("Diagnostics", "TraceEnabled", 1, "AFX.INI") != 0;
    state->trace_flags = static_cast<unsigned>(
        GetPrivateProfileIntA("Diagnostics", "TraceFlags", 0, "AFX.INI"));
#else
    state->trace_enabled = true;
    state->trace_flags = 0;
#endif
    g_debug_state = *state;
    state->previous_dump_client = set_dump_client(AfxDumpClientBridge);
    state->previous_report_hook = set_report_hook(AfxReportHookBridge);
    CrtSetReportMode(2, 4);
    state->initialized = true;
    g_debug_state = *state;
    return state;
}

void InitializeAfxDebugSupport() {
    InitializeAfxDebugSupportNoop();
    RegisterAfxDebugStateCleanup();
}

void InitializeAfxDebugSupportNoop() {
}

void RegisterAfxDebugStateCleanup() {
    CrtAtexit(CleanupAfxDebugStateAtExit);
}

void CleanupAfxDebugStateAtExit() {
    DestroyProcessLocalAfxDebugState(&g_debug_state);
}

bool EnsureAfxDebugState() {
    return GetProcessLocalAfxDebugState() != nullptr;
}

AfxDebugState* DeleteAfxDebugState(AfxDebugState* state, unsigned flags) {
    if (state == nullptr) {
        return nullptr;
    }
    DestroyProcessLocalAfxDebugState(state);
    if ((flags & 1u) != 0 && state != &g_debug_state) {
        delete state;
    }
    return state;
}

AfxDebugState* ConstructMfcNoTrackObjectBase(AfxDebugState* state) {
    if (state != nullptr) {
        state->initialized = false;
    }
    return state;
}

void DestroyProcessLocalAfxDebugState(AfxDebugState* state) {
    if (state == nullptr || !state->initialized) {
        return;
    }
    CrtDumpMemoryLeaks();
    const int debug_flag = CrtSetDebugFlag(-1);
    CrtSetDebugFlag(debug_flag & ~0x20);
    set_report_hook(state->previous_report_hook);
    set_dump_client(state->previous_dump_client);
    state->initialized = false;
    g_debug_state_created = false;
}

AfxDebugState* GetProcessLocalAfxDebugState() {
    if (!g_debug_state_created) {
        ConstructAfxDebugState(&g_debug_state);
        g_debug_state_created = g_debug_state.initialized;
    }
    return g_debug_state_created ? &g_debug_state : nullptr;
}

void Dump(const MfcObjectCompat* object, MfcDumpContext* context) {
    MfcDumpContext& dump_context =
        context == nullptr ? g_dump_context : *context;
    if (object == nullptr) {
        DumpContextWriteString(dump_context, "NULL\n");
        return;
    }

    const char* class_name = object->runtime_class == nullptr
        ? "CObject"
        : object->runtime_class->class_name;
    DumpContextWriteString(dump_context, "a ");
    DumpContextWriteString(dump_context, class_name);
    DumpContextWriteString(dump_context, " at ");
    DumpContextWritePointer(dump_context, object);
    DumpContextWriteString(dump_context, "\n");
}

} // namespace ranker
