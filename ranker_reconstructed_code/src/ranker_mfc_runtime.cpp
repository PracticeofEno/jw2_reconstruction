#include "ranker_mfc_runtime.h"

#include "ranker_mfc_dialog_template_internal.h"

#include "ranker_crt_runtime.h"
#include "ranker_dpg_archive.h"
#include "ranker_miles.h"
#include "ranker_ole_datetime.h"
#include "ranker_ole_variant.h"
#include "ranker_win32_compat.h"

#include <array>
#include <cstdarg>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dde.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace ranker {

void MfcDebugDeleteClientBlock(void* memory);
void MfcDebugDeleteNormalBlock(void* memory);
OleDateTimeCompat ConstructOleDateTimeFromTimeT(std::time_t value);

namespace {



#ifdef _WIN32
constexpr WPARAM kMfcHelpCommand = 0xe146;
constexpr WORD kCommonDialogHelpButton = IDHELP;
constexpr UINT kMfcSetMessageStringMessage = 0x0362;
constexpr UINT kMfcIdleUpdateCmdUiMessage = 0x0363;
constexpr UINT kMfcInitialUpdateMessage = 0x0364;
constexpr UINT kMfcKickIdleMessage = 0x036a;
constexpr UINT kMfcFloatingFrameMessage = 0x036d;
constexpr UINT kMfcActivateTopLevelMessage = 0x036e;
constexpr UINT kMfcIdFileNew = 0xe100;
constexpr UINT kMfcIdFileOpen = 0xe101;
constexpr UINT kMfcIdNextPane = 0xe150;
constexpr UINT kMfcIdPrevPane = 0xe151;
constexpr UINT kMfcIdControlBarFirst = 0xe800;
constexpr UINT kMfcIdControlBarLast = 0xe81f;
constexpr unsigned kMfcWndFlagTopLevelActive = 0x0020;
constexpr unsigned kMfcWndFlagKeepMiniActive = 0x0200;
constexpr unsigned kMfcModalContinueFlag = 0x10;
constexpr unsigned kMfcModalLoopFlag = 0x08;
constexpr DWORD kMfcDropEffectNone = 0x00000000UL;
constexpr DWORD kMfcDropEffectScroll = 0x80000000UL;
constexpr DWORD kMfcDropEffectUseDefault = 0xffffffffUL;
thread_local MfcWinThreadCompat* g_current_win_thread = nullptr;
thread_local MfcCWndCompat* g_pending_create_window = nullptr;
thread_local HHOOK g_cbt_hook = nullptr;
thread_local std::string g_registered_window_class_name;
MfcWinThreadCompat* g_app_win_thread = nullptr;
MfcWinThreadCompat g_fallback_thread_state;
HINSTANCE g_resource_handle = nullptr;
UINT g_drag_list_message = 0;
unsigned g_cwnd_static_cleanup_flags = 0;
unsigned g_deferred_registered_classes = 0;
MfcCWndCompat g_cwnd_wnd_top;
MfcCWndCompat g_cwnd_wnd_bottom;
MfcCWndCompat g_cwnd_wnd_top_most;
MfcCWndCompat g_cwnd_wnd_no_top_most;
MfcWindowHandleMapCompat g_window_handle_map;
bool g_window_handle_map_created = false;
std::vector<std::unique_ptr<MfcCWndCompat>> g_temporary_windows;
MfcMenuHandleMapCompat g_menu_handle_map;
bool g_menu_handle_map_created = false;
std::vector<std::unique_ptr<MfcMenuCompat>> g_temporary_menus;
std::unordered_map<HDC, MfcCDCCompat*> g_dc_handle_map;
std::vector<std::unique_ptr<MfcCDCCompat>> g_temporary_dcs;
MfcHandleMapCompat g_gdi_object_handle_map;
bool g_gdi_object_handle_map_created = false;
std::vector<std::unique_ptr<MfcGdiObjectCompat>> g_temporary_gdi_objects;
HBRUSH g_halftone_brush = nullptr;
std::vector<MfcDocTemplateCompat*> g_doc_template_registry;
RECT g_frame_rect_default{LONG_MIN, LONG_MIN, 0, 0};
UINT g_registered_mouse_wheel_message = 0;
std::vector<MfcFrameWndCompat*> g_frame_windows;
HBITMAP g_mini_frame_caption_bitmap = nullptr;
HFONT g_mini_frame_caption_font = nullptr;
SIZE g_mini_frame_caption_bitmap_size{16, 15};
bool g_mini_frame_metrics_cleanup_registered = false;
void* g_mfc_critical_memory_pool = nullptr;
int g_temp_map_lock_count = 0;
int g_wait_cursor_count = 0;
HCURSOR g_previous_wait_cursor = nullptr;
#endif

#ifdef _WIN32
MfcWinThreadCompat& ole_thread_state() {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    return thread == nullptr ? g_fallback_thread_state : *thread;
}
#endif



#ifdef _WIN32
void send_message_to_descendants(HWND parent, UINT message, WPARAM wparam,
    LPARAM lparam) {
    if (parent == nullptr) {
        return;
    }
    for (HWND child = GetTopWindow(parent); child != nullptr;
         child = GetNextWindow(child, GW_HWNDNEXT)) {
        SendMessageA(child, message, wparam, lparam);
        send_message_to_descendants(child, message, wparam, lparam);
    }
}

bool is_dialog_input_message(UINT message) {
    return (WM_KEYFIRST <= message && message <= WM_KEYLAST) ||
        (WM_MOUSEFIRST <= message && message <= WM_MOUSELAST);
}

bool modal_show_on_idle(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return false;
    }
    ShowWindow(window.window, SW_SHOWNORMAL);
    UpdateWindow(window.window);
    return true;
}

INT_PTR CALLBACK reconstructed_dialog_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        return AfxDlgProcCompat(hwnd, message, wparam, lparam);
    }
    MfcCWndCompat* window = CWndFromHandlePermanent(hwnd);
    if (window == nullptr) {
        return FALSE;
    }
    LRESULT result = 0;
    return CWndOnWndMsg(*window, message, wparam, lparam, &result)
        ? static_cast<INT_PTR>(result) : FALSE;
}


#endif

} // namespace

#ifdef _WIN32
INT_PTR AfxDlgProcCompat(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    (void)wparam;
    (void)lparam;
    if (message != WM_INITDIALOG) {
        return FALSE;
    }

    MfcObjectCompat* object = CWndFromHandlePermanent(hwnd);
    auto* dialog = static_cast<MfcDialogCompat*>(
        AfxDynamicDownCast(GetDialogRuntimeClass(), object));
    if (dialog == nullptr) {
        return TRUE;
    }
    return static_cast<INT_PTR>(DialogHandleInitDialog(*dialog));
}

#endif



namespace {

template <typename Type>
MfcRuntimeClassCompat* SimpleRuntimeClass(const char* class_name) {
    static MfcRuntimeClassCompat runtime_class{
        class_name, static_cast<int>(sizeof(Type)), 0xffff,
        +[]() -> void* { return new Type(); },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* RegisterRuntimeClass(
    MfcRuntimeClassCompat* runtime_class) {
    return AfxClassInitObject(runtime_class);
}

struct DllVersionInfoCompat {
    DWORD size = sizeof(DllVersionInfoCompat);
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    DWORD platform = 0;
};

struct MfcComboBoxExRuntimeCompat : MfcControlCompat {};

struct MfcTempImageListRuntimeCompat : MfcImageListCompat {};

struct MfcPropPageFontInfoCompat {
    std::string face_name;
    short point_size = 0;
};

struct MfcSimpleListCompat {
    int element_size = 0;
    void* head = nullptr;
};

struct MfcThreadLocalObjectCompat {
    void* data = nullptr;
};

struct MfcAuxModuleStateCompat : MfcObjectCompat {
    IUnknown* primary_object = nullptr;
    IUnknown* secondary_object = nullptr;
    LONG reference_count = 1;
    std::vector<MfcSimpleListCompat> simple_lists;
};

struct MfcBaseModuleStateCompat : MfcAuxModuleStateCompat {
    bool ole_enabled = false;
    MfcOccManagerCompat* occ_manager = nullptr;
};

struct MfcOleStateCompat : MfcObjectCompat {
    HMODULE library = nullptr;
    void (*cleanup)(void*) = nullptr;
    void* cleanup_context = nullptr;
    void (*on_term)(void) = nullptr;
};

struct MfcAmbientCacheCompat : MfcObjectCompat {
    void* cached_object = nullptr;
};

struct MfcTempWndRuntimeCompat : MfcCWndCompat {};

struct MfcTempMenuRuntimeCompat : MfcMenuCompat {};

struct MfcOleMessageFilterCompat : MfcCommandTargetCompat {
    DWORD busy_reply = 0;
    DWORD retry_reply = 0;
    DWORD message_pending = 1;
    DWORD busy_timeout = 0;
    DWORD retry_timeout = 0;
    DWORD message_pending_delay = 0;
    DWORD flags = 0;
    DWORD registered = 0;
    DWORD last_tick = 0;
    DWORD initial_flags = 0;
    POINT last_cursor{};
    DWORD reserved = 0;
};

struct MfcOleExceptionCompat : MfcSimpleExceptionCompat {
    SCODE scode = S_OK;
};

struct MfcOleInnerUnknownCompat {
    std::array<unsigned char, 0x0c> inner_unknown_adjustor{};
    IUnknown* controlling_unknown = nullptr;
    LONG reference_count = 1;
};

struct MfcOleDispatchDriverCompat {
    IDispatch* dispatch = nullptr;
    bool auto_release = false;
};

using COleVariant = VARIANTARG;
using COleSafeArray = VARIANTARG;

bool& CommonCtlOemMbcsFlag() {
    static bool value = false;
    return value;
}

MfcAuxDataCompat& GlobalMfcAuxData() {
    static MfcAuxDataCompat aux_data;
    return aux_data;
}

} // namespace

namespace {
struct MfcCommandLineInfoRuntimeCompat;
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9ce0();
void MfcThreadSlotRuntime_005eab60(int slot);
void* MfcThreadSlotRuntime_005eabd3(int slot);
void MfcThreadSlotRuntime_005eb878(int* slot);
void MfcWinAppThreadRuntime_005ee5fe(
    MfcCommandLineInfoRuntimeCompat* info);
void MfcWinAppThreadRuntime_005ee8f1(MfcWinAppCompat* app);

MfcRuntimeClassCompat* GetArchiveExceptionRuntimeClass() {
    return SimpleRuntimeClass<MfcSimpleExceptionCompat>("CArchiveException");
}

MfcRuntimeClassCompat* GetFileExceptionRuntimeClass() {
    return SimpleRuntimeClass<MfcFileExceptionCompat>("CFileException");
}

MfcRuntimeClassCompat* GetByteArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcByteArrayCompat>("CByteArray");
}

MfcRuntimeClassCompat* GetWordArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcWordArrayCompat>("CWordArray");
}

MfcRuntimeClassCompat* GetDWordArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcDWordArrayCompat>("CDWordArray");
}

MfcRuntimeClassCompat* GetUIntArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcUIntArrayCompat>("CUIntArray");
}

MfcRuntimeClassCompat* GetPtrArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcPtrArrayCompat>("CPtrArray");
}

MfcRuntimeClassCompat* GetObArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcObArrayCompat>("CObArray");
}

MfcRuntimeClassCompat* GetCStringArrayRuntimeClass() {
    return SimpleRuntimeClass<MfcCStringArrayCompat>("CStringArray");
}

MfcRuntimeClassCompat* GetMapWordToPtrRuntimeClass() {
    return SimpleRuntimeClass<MfcMapWordToPtrCompat>("CMapWordToPtr");
}

MfcRuntimeClassCompat* GetMapPtrToWordRuntimeClass() {
    return SimpleRuntimeClass<MfcMapPtrToWordCompat>("CMapPtrToWord");
}

MfcRuntimeClassCompat* GetMapWordToObRuntimeClass() {
    return SimpleRuntimeClass<MfcMapWordToObCompat>("CMapWordToOb");
}

MfcRuntimeClassCompat* GetMapStringToPtrRuntimeClass() {
    return SimpleRuntimeClass<MfcMapStringToPtrCompat>("CMapStringToPtr");
}

MfcRuntimeClassCompat* GetMapStringToObRuntimeClass() {
    return SimpleRuntimeClass<MfcMapStringToObCompat>("CMapStringToOb");
}

MfcRuntimeClassCompat* GetMapStringToStringRuntimeClass() {
    return SimpleRuntimeClass<MfcMapStringToStringCompat>(
        "CMapStringToString");
}

MfcRuntimeClassCompat* GetFileDialogRuntimeClass() {
    return SimpleRuntimeClass<MfcFileDialogCompat>("CFileDialog");
}

namespace {

MfcSimpleExceptionCompat& GlobalResourceExceptionRuntime() {
    static MfcSimpleExceptionCompat exception;
    return exception;
}

MfcSimpleExceptionCompat& GlobalUserExceptionRuntime() {
    static MfcSimpleExceptionCompat exception;
    return exception;
}

MfcSimpleExceptionCompat& GlobalMemoryExceptionRuntime() {
    static MfcSimpleExceptionCompat exception;
    return exception;
}

MfcSimpleExceptionCompat& GlobalNotSupportedExceptionRuntime() {
    static MfcSimpleExceptionCompat exception;
    return exception;
}

void SetSimpleExceptionRuntimeMessage(MfcSimpleExceptionCompat& exception,
    const char* message) {
    if (message == nullptr) {
        return;
    }
    std::strncpy(exception.message.data(), message,
        exception.message.size() - 1);
    exception.has_message = exception.message[0] != '\0';
    exception.string_initialized = true;
}

MfcSimpleExceptionCompat& ConstructResourceExceptionRuntime(
    MfcSimpleExceptionCompat& exception) {
    ConstructSimpleException(exception);
    SetSimpleExceptionRuntimeMessage(exception, "resource exception");
    return exception;
}

MfcRuntimeClassCompat* GetExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CException", static_cast<int>(sizeof(MfcSimpleExceptionCompat)),
        0xffff, nullptr, GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetResourceExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CResourceException",
        static_cast<int>(sizeof(MfcSimpleExceptionCompat)), 0xf022,
        +[]() -> void* {
            auto* exception = new MfcSimpleExceptionCompat();
            ConstructResourceExceptionRuntime(*exception);
            return exception;
        },
        GetExceptionRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetUserExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CUserException", static_cast<int>(sizeof(MfcSimpleExceptionCompat)),
        0xf024,
        +[]() -> void* {
            auto* exception = new MfcSimpleExceptionCompat();
            ConstructUserException(*exception, 0, "user exception");
            return exception;
        },
        GetExceptionRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetMemoryExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CMemoryException",
        static_cast<int>(sizeof(MfcSimpleExceptionCompat)), 0xf023,
        +[]() -> void* {
            auto* exception = new MfcSimpleExceptionCompat();
            ConstructMemoryException(*exception);
            return exception;
        },
        GetExceptionRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetNotSupportedExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CNotSupportedException",
        static_cast<int>(sizeof(MfcSimpleExceptionCompat)), 0xf021,
        +[]() -> void* {
            auto* exception = new MfcSimpleExceptionCompat();
            ConstructNotSupportedException(*exception);
            return exception;
        },
        GetExceptionRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetOleExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "COleException", static_cast<int>(sizeof(MfcOleExceptionCompat)),
        0xffff,
        +[]() -> void* {
            auto* exception = new MfcOleExceptionCompat();
            ConstructSimpleException(*exception);
            exception->scode = S_OK;
            return exception;
        },
        GetExceptionRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetCdcRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CDC", static_cast<int>(sizeof(MfcCDCCompat)), 0xffff,
        +[]() -> void* {
            auto* dc = new MfcCDCCompat();
            dc->runtime_class = GetCdcRuntimeClassCompat();
            return dc;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetClientDcRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CClientDC", static_cast<int>(sizeof(MfcWindowDCCompat)), 0xffff,
        +[]() -> void* {
            auto* dc = new MfcWindowDCCompat();
            dc->runtime_class = GetClientDcRuntimeClassCompat();
            return dc;
        },
        GetCdcRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetWindowDcRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CWindowDC", static_cast<int>(sizeof(MfcWindowDCCompat)), 0xffff,
        +[]() -> void* {
            auto* dc = new MfcWindowDCCompat();
            dc->runtime_class = GetWindowDcRuntimeClassCompat();
            return dc;
        },
        GetCdcRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetPaintDcRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CPaintDC", static_cast<int>(sizeof(MfcWindowDCCompat)), 0xffff,
        +[]() -> void* {
            auto* dc = new MfcWindowDCCompat();
            dc->runtime_class = GetPaintDcRuntimeClassCompat();
            return dc;
        },
        GetCdcRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetGdiObjectRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CGdiObject", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructGdiObjectCompat(*object);
            object->runtime_class = GetGdiObjectRuntimeClassCompat();
            return object;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetPenRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CPen", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructPen(*object);
            object->runtime_class = GetPenRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetBrushRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CBrush", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructBrush(*object);
            object->runtime_class = GetBrushRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetFontRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CFont", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructFont(*object);
            object->runtime_class = GetFontRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetBitmapRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CBitmap", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructBitmap(*object);
            object->runtime_class = GetBitmapRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetPaletteRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CPalette", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructPalette(*object);
            object->runtime_class = GetPaletteRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetRegionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CRgn", static_cast<int>(sizeof(MfcGdiObjectCompat)), 0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructRgn(*object);
            object->runtime_class = GetRegionRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetTempDcRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CTempDC", static_cast<int>(sizeof(MfcCDCCompat)), 0xffff,
        +[]() -> void* {
            auto* dc = new MfcCDCCompat();
            dc->runtime_class = GetTempDcRuntimeClassCompat();
            return dc;
        },
        GetCdcRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetTempGdiObjectRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CTempGdiObject", static_cast<int>(sizeof(MfcGdiObjectCompat)),
        0xffff,
        +[]() -> void* {
            auto* object = new MfcGdiObjectCompat();
            ConstructGdiObjectCompat(*object);
            object->runtime_class = GetTempGdiObjectRuntimeClassCompat();
            return object;
        },
        GetGdiObjectRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

void* CreateMdiChildThreadRuntimeObject() {
    auto* window = new MfcCWndCompat();
    ConstructCWnd(*window);
    return window;
}

int& MdiChildThreadRuntimeSlot() {
    static int slot = 0;
    return slot;
}

} // namespace

int MfcThreadSlotRuntime_005eb63c(int* slot,
    MfcCreateObjectCallback create_object);
void MfcExceptionRuntimeThunk_005e8a3f();
void MfcExceptionRuntimeThunk_005e8a55();
void MfcExceptionRuntimeThunk_005e8a67();
void MfcExceptionRuntimeThunk_005e8a95();
void MfcExceptionRuntimeThunk_005e8aab();
void MfcExceptionRuntimeThunk_005e8abd();
void MfcExceptionRuntimeThunk_005e8d80(MfcCDCCompat* dc);
void MfcExceptionRuntimeThunk_005e8df0(MfcGdiObjectCompat* object);
void MfcExceptionRuntimeThunk_005e8e4f();
void MfcExceptionRuntimeThunk_005e8e65();
void MfcExceptionRuntimeThunk_005e8e77();
void MfcExceptionRuntimeThunk_005e8ea5();
void MfcExceptionRuntimeThunk_005e8ebb();
void MfcExceptionRuntimeThunk_005e8ecd();
int MfcExceptionRuntimeThunk_005e8f00(void* context = nullptr);

MfcSimpleExceptionCompat* ConstructSimpleExceptionVTable006eba2c(
    MfcSimpleExceptionCompat* exception) {
    if (exception != nullptr) {
        ConstructResourceExceptionRuntime(*exception);
    }
    return exception;
}

void DestroySimpleExceptionVTable006eba2c(
    MfcSimpleExceptionCompat* exception) {
    if (exception != nullptr) {
        DestroySimpleException(*exception);
    }
}

MfcSimpleExceptionCompat* ConstructSimpleExceptionVTable006eba4c(
    MfcSimpleExceptionCompat* exception) {
    if (exception != nullptr) {
        ConstructUserException(*exception, 0, "user exception");
    }
    return exception;
}

void DestroySimpleExceptionVTable006eba4c(
    MfcSimpleExceptionCompat* exception) {
    if (exception != nullptr) {
        DestroyUserException(*exception);
    }
}

MfcSimpleExceptionCompat* DeleteSimpleExceptionVTable006eba2cScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroySimpleExceptionVTable006eba2c(exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

MfcSimpleExceptionCompat* DeleteSimpleExceptionVTable006eba4cScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    DestroySimpleExceptionVTable006eba4c(exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8a20() {
    return GetResourceExceptionRuntimeClassCompat();
}

void MfcExceptionRuntimeThunk_005e8a30() {
    MfcExceptionRuntimeThunk_005e8a3f();
    MfcExceptionRuntimeThunk_005e8a55();
}

void MfcExceptionRuntimeThunk_005e8a3f() {
    ConstructSimpleExceptionVTable006eba2c(&GlobalResourceExceptionRuntime());
    RegisterRuntimeClass(GetResourceExceptionRuntimeClassCompat());
}

void MfcExceptionRuntimeThunk_005e8a55() {
    CrtAtexit(MfcExceptionRuntimeThunk_005e8a67);
}

void MfcExceptionRuntimeThunk_005e8a67() {
    DestroySimpleExceptionVTable006eba2c(&GlobalResourceExceptionRuntime());
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8a76() {
    return GetUserExceptionRuntimeClassCompat();
}

void MfcExceptionRuntimeThunk_005e8a86() {
    MfcExceptionRuntimeThunk_005e8a95();
    MfcExceptionRuntimeThunk_005e8aab();
}

void MfcExceptionRuntimeThunk_005e8a95() {
    ConstructSimpleExceptionVTable006eba4c(&GlobalUserExceptionRuntime());
    RegisterRuntimeClass(GetUserExceptionRuntimeClassCompat());
}

void MfcExceptionRuntimeThunk_005e8aab() {
    CrtAtexit(MfcExceptionRuntimeThunk_005e8abd);
}

void MfcExceptionRuntimeThunk_005e8abd() {
    DestroySimpleExceptionVTable006eba4c(&GlobalUserExceptionRuntime());
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8b30() {
    return GetCdcRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8b40() {
    return GetClientDcRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8b50() {
    return GetWindowDcRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8b60() {
    return GetPaintDcRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8bd4() {
    return GetGdiObjectRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8be4() {
    return GetPenRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8bf4() {
    return GetBrushRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8c04() {
    return GetFontRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8c14() {
    return GetBitmapRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8c24() {
    return GetPaletteRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8c34() {
    return GetRegionRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8ca8() {
    return GetTempDcRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8d1c() {
    return GetTempGdiObjectRuntimeClassCompat();
}

MfcCDCCompat* MfcExceptionRuntimeThunk_005e8d50(
    MfcCDCCompat* dc, unsigned flags) {
    if (dc == nullptr) {
        return nullptr;
    }
    MfcExceptionRuntimeThunk_005e8d80(dc);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dc);
    }
    return dc;
}

void MfcExceptionRuntimeThunk_005e8d80(MfcCDCCompat* dc) {
    if (dc != nullptr) {
        DestroyCDC(*dc);
    }
}

MfcGdiObjectCompat* MfcExceptionRuntimeThunk_005e8da0(
    MfcGdiObjectCompat* object) {
    if (object != nullptr) {
        ConstructGdiObjectCompat(*object);
        object->runtime_class = GetTempGdiObjectRuntimeClassCompat();
    }
    return object;
}

MfcGdiObjectCompat* MfcExceptionRuntimeThunk_005e8dc0(
    MfcGdiObjectCompat* object, unsigned flags) {
    if (object == nullptr) {
        return nullptr;
    }
    MfcExceptionRuntimeThunk_005e8df0(object);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(object);
    }
    return object;
}

void MfcExceptionRuntimeThunk_005e8df0(MfcGdiObjectCompat* object) {
    if (object != nullptr) {
        DestroyGdiObjectCompat(*object);
    }
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8e10() {
    return GetOleExceptionRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8e20() {
    return GetExceptionRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8e30() {
    return GetMemoryExceptionRuntimeClassCompat();
}

void MfcExceptionRuntimeThunk_005e8e40() {
    MfcExceptionRuntimeThunk_005e8e4f();
    MfcExceptionRuntimeThunk_005e8e65();
}

void MfcExceptionRuntimeThunk_005e8e4f() {
    ConstructMemoryException(GlobalMemoryExceptionRuntime());
    RegisterRuntimeClass(GetMemoryExceptionRuntimeClassCompat());
}

void MfcExceptionRuntimeThunk_005e8e65() {
    CrtAtexit(MfcExceptionRuntimeThunk_005e8e77);
}

void MfcExceptionRuntimeThunk_005e8e77() {
    DestroyMemoryException(GlobalMemoryExceptionRuntime());
}

MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8e86() {
    return GetNotSupportedExceptionRuntimeClassCompat();
}

void MfcExceptionRuntimeThunk_005e8e96() {
    MfcExceptionRuntimeThunk_005e8ea5();
    MfcExceptionRuntimeThunk_005e8ebb();
}

void MfcExceptionRuntimeThunk_005e8ea5() {
    ConstructNotSupportedException(GlobalNotSupportedExceptionRuntime());
    RegisterRuntimeClass(GetNotSupportedExceptionRuntimeClassCompat());
}

void MfcExceptionRuntimeThunk_005e8ebb() {
    CrtAtexit(MfcExceptionRuntimeThunk_005e8ecd);
}

void MfcExceptionRuntimeThunk_005e8ecd() {
    DestroyNotSupportedException(GlobalNotSupportedExceptionRuntime());
}

int MfcExceptionRuntimeThunk_005e8ee0(void*) {
    return MfcExceptionRuntimeThunk_005e8f00();
}

int MfcExceptionRuntimeThunk_005e8f00(void*) {
    return MfcThreadSlotRuntime_005eb63c(&MdiChildThreadRuntimeSlot(),
        CreateMdiChildThreadRuntimeObject);
}

MfcOleStateCompat* MfcToolBarRuntime_005ec4f7() {
    return MfcAuxAppRuntime_005e9ce0();
}

void MfcToolBarRuntime_005ec515() {}

void MfcToolBarRuntime_005ec570(void* slot) {
    MfcThreadSlotRuntime_005eb878(static_cast<int*>(slot));
}

void MfcToolBarRuntime_005ec52c() {
    MfcToolBarRuntime_005ec570(nullptr);
}

void MfcToolBarRuntime_005ec51a() {
    CrtAtexit(MfcToolBarRuntime_005ec52c);
}

void MfcToolBarRuntime_005ec506() {
    MfcToolBarRuntime_005ec515();
    MfcToolBarRuntime_005ec51a();
}

void MfcToolBarRuntime_005ec54a() {}

void MfcToolBarRuntime_005ec590(void* slot) {
    MfcThreadSlotRuntime_005eb878(static_cast<int*>(slot));
}

void MfcToolBarRuntime_005ec561() {
    MfcToolBarRuntime_005ec590(nullptr);
}

void MfcToolBarRuntime_005ec54f() {
    CrtAtexit(MfcToolBarRuntime_005ec561);
}

void MfcToolBarRuntime_005ec53b() {
    MfcToolBarRuntime_005ec54a();
    MfcToolBarRuntime_005ec54f();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec5b0() {
    return GetArchiveExceptionRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec5c0() {
    return GetFileExceptionRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec63e() {
    return GetByteArrayRuntimeClass();
}

void MfcToolBarRuntime_005ec658() {
    RegisterRuntimeClass(GetByteArrayRuntimeClass());
}

void MfcToolBarRuntime_005ec64e() {
    MfcToolBarRuntime_005ec658();
}

MfcArchiveCompat* MfcToolBarRuntime_005ec66c(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetByteArrayRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec6fe() {
    return GetWordArrayRuntimeClass();
}

void MfcToolBarRuntime_005ec718() {
    RegisterRuntimeClass(GetWordArrayRuntimeClass());
}

void MfcToolBarRuntime_005ec70e() {
    MfcToolBarRuntime_005ec718();
}

MfcArchiveCompat* MfcToolBarRuntime_005ec72c(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetWordArrayRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec7be() {
    return GetDWordArrayRuntimeClass();
}

void MfcToolBarRuntime_005ec7d8() {
    RegisterRuntimeClass(GetDWordArrayRuntimeClass());
}

void MfcToolBarRuntime_005ec7ce() {
    MfcToolBarRuntime_005ec7d8();
}

MfcArchiveCompat* MfcToolBarRuntime_005ec7ec(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetDWordArrayRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec810() {
    return GetUIntArrayRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec820() {
    return GetPtrArrayRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec89e() {
    return GetObArrayRuntimeClass();
}

void MfcToolBarRuntime_005ec8b8() {
    RegisterRuntimeClass(GetObArrayRuntimeClass());
}

void MfcToolBarRuntime_005ec8ae() {
    MfcToolBarRuntime_005ec8b8();
}

MfcArchiveCompat* MfcToolBarRuntime_005ec8cc(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetObArrayRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec95e() {
    return GetCStringArrayRuntimeClass();
}

void MfcToolBarRuntime_005ec978() {
    RegisterRuntimeClass(GetCStringArrayRuntimeClass());
}

void MfcToolBarRuntime_005ec96e() {
    MfcToolBarRuntime_005ec978();
}

MfcArchiveCompat* MfcToolBarRuntime_005ec98c(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetCStringArrayRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec9b0() {
    return GetMapWordToPtrRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ec9c0() {
    return GetMapPtrToWordRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005eca40() {
    return GetMapWordToObRuntimeClass();
}

void MfcToolBarRuntime_005eca5a() {
    RegisterRuntimeClass(GetMapWordToObRuntimeClass());
}

void MfcToolBarRuntime_005eca50() {
    MfcToolBarRuntime_005eca5a();
}

MfcArchiveCompat* MfcToolBarRuntime_005eca6e(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetMapWordToObRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005eca90() {
    return GetMapStringToPtrRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecb10() {
    return GetMapStringToObRuntimeClass();
}

void MfcToolBarRuntime_005ecb2a() {
    RegisterRuntimeClass(GetMapStringToObRuntimeClass());
}

void MfcToolBarRuntime_005ecb20() {
    MfcToolBarRuntime_005ecb2a();
}

MfcArchiveCompat* MfcToolBarRuntime_005ecb3e(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetMapStringToObRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecbd0() {
    return GetMapStringToStringRuntimeClass();
}

void MfcToolBarRuntime_005ecbea() {
    RegisterRuntimeClass(GetMapStringToStringRuntimeClass());
}

void MfcToolBarRuntime_005ecbe0() {
    MfcToolBarRuntime_005ecbea();
}

MfcArchiveCompat* MfcToolBarRuntime_005ecbfe(MfcArchiveCompat* archive,
    MfcObjectCompat** object) {
    *object = ArchiveReadObject(*archive, GetMapStringToStringRuntimeClass());
    return archive;
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecc20() {
    return GetDialogRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecc30() {
    return GetFileDialogRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecc40() {
    return GetPropertyPageRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecc50() {
    return GetPropertySheetRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecc60() {
    return GetPropertyPageExRuntimeClass();
}

MfcRuntimeClassCompat* MfcToolBarRuntime_005ecc70() {
    return GetPropertySheetExRuntimeClass();
}

MfcControlBarCompat& MfcToolBarRuntime_005ecc80(
    MfcControlBarCompat& bar) {
    return ConstructControlBar(bar);
}

void MfcToolBarRuntime_005ecd3f(MfcControlBarCompat& bar, int left,
    int top, int right, int bottom) {
    ControlBarSetBordersLTRB(bar, left, top, right, bottom);
}

BOOL MfcToolBarRuntime_005ecdea(MfcControlBarCompat& bar,
    MfcCWndCompat& owner) {
    bar.owner_frame = &owner;
    if (bar.window != nullptr) {
        SetWindowLongA(bar.window, GWL_STYLE,
            GetWindowLongA(bar.window, GWL_STYLE) | WS_CLIPSIBLINGS);
    }
    return owner.window == nullptr ? FALSE : TRUE;
}

void MfcToolBarRuntime_005eceda(MfcControlBarCompat& bar, DWORD style) {
    const DWORD old_style = bar.bar_style;
    bar.bar_style = style & 0x0040ffffUL;
    if (old_style != bar.bar_style &&
        ObjectIsKindOfRuntimeClass(&bar, GetToolBarRuntimeClass())) {
        ToolBarOnBarStyleChange(static_cast<MfcToolBarCompat&>(bar),
            old_style, bar.bar_style);
    }
}

void MfcToolBarRuntime_005ecf58() {}

bool MfcToolBarRuntime_005ecf65(MfcControlBarCompat& bar,
    std::size_t count, std::size_t element_size) {
    if (count != 0 && element_size == 0) {
        return false;
    }
    void* storage = count == 0 ? nullptr : std::calloc(count, element_size);
    if (count != 0 && storage == nullptr) {
        return false;
    }
    std::free(bar.item_data);
    bar.item_data = storage;
    bar.count = static_cast<int>(count);
    return true;
}

MfcControlBarCompat* MfcToolBarRuntime_005ed050(
    MfcControlBarCompat* bar, unsigned flags) {
    return DeleteControlBarScalarDtor(bar, flags);
}

DWORD MfcToolBarRuntime_005ed080() {
    static DWORD version = 0xffffffffUL;
    if (version != 0xffffffffUL) {
        return version;
    }

    version = 0x00040000UL;
    HMODULE module = GetModuleHandleA("COMCTL32.DLL");
    if (module != nullptr) {
        using DllGetVersionProc = HRESULT (CALLBACK *)(DllVersionInfoCompat*);
        auto* proc = reinterpret_cast<DllGetVersionProc>(
            GetProcAddress(module, "DllGetVersion"));
        if (proc != nullptr) {
            DllVersionInfoCompat info{};
            if (SUCCEEDED(proc(&info)) && info.major <= 0xffff &&
                info.minor <= 0xffff) {
                version = (info.major << 16) | (info.minor & 0xffffU);
            }
        }
    }
    return version;
}

int MfcToolBarRuntime_005ed186() {
    static int marlett_width = -1;
    if (marlett_width >= 0) {
        return marlett_width;
    }

    marlett_width = 0;
    HDC dc = GetDC(nullptr);
    if (dc == nullptr) {
        return marlett_width;
    }
    HFONT font = CreateFontA(GetSystemMetrics(SM_CYMENUCHECK), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, SYMBOL_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Marlett");
    HGDIOBJ old_font = font == nullptr ? nullptr : SelectObject(dc, font);
    int width = 0;
    if (GetCharWidthA(dc, '6', '6', &width) != FALSE) {
        marlett_width = width;
    }
    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    ReleaseDC(nullptr, dc);
    return marlett_width;
}

MfcToolBarCompat& MfcToolBarRuntime_005ed2a0(MfcToolBarCompat& toolbar) {
    ConstructControlBar(toolbar);
    toolbar.runtime_class = GetToolBarRuntimeClass();
    toolbar.image_instance = nullptr;
    toolbar.image_resource = nullptr;
    toolbar.image_well = nullptr;
    toolbar.button_layout_dirty = true;
    toolbar.button_size = SIZE{23, 22};
    toolbar.image_size = SIZE{16, 15};
    toolbar.cx_left_border = 3;
    toolbar.cx_right_border = 3;
    return toolbar;
}

bool MfcToolBarRuntime_005ed484(MfcToolBarCompat& toolbar,
    MfcCWndCompat& parent, DWORD style, const RECT& rect, UINT id) {
    if (parent.window == nullptr) {
        return false;
    }
    MfcToolBarRuntime_005ed080();
    MfcToolBarRuntime_005ed186();
    INITCOMMONCONTROLSEX init{};
    init.dwSize = sizeof(init);
    init.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&init);
    toolbar.bar_style = style & 0x0040ffffUL;
    HWND window = CreateWindowExA(0, TOOLBARCLASSNAMEA, nullptr,
        (style | WS_CHILD) & ~WS_VISIBLE, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top, parent.window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        GetModuleHandleA(nullptr), nullptr);
    if (window == nullptr) {
        return false;
    }
    AttachCWndHandle(toolbar, window);
    SendMessageA(window, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    ToolBarSetSizes(toolbar, toolbar.button_size, toolbar.image_size);
    return true;
}

bool MfcToolBarRuntime_005ed420(MfcToolBarCompat& toolbar,
    MfcCWndCompat& parent, DWORD style, UINT id) {
    RECT rect{toolbar.cx_left_border, toolbar.cy_top_border,
        toolbar.cx_right_border, toolbar.cy_bottom_border};
    return MfcToolBarRuntime_005ed484(toolbar, parent, style, rect, id);
}

MfcRuntimeClassCompat* GetColorDialogRuntimeClass() {
    return SimpleRuntimeClass<MfcColorDialogCompat>("CColorDialog");
}

MfcRuntimeClassCompat* GetCheckListBoxRuntimeClass() {
    return SimpleRuntimeClass<MfcCheckListBoxCompat>("CCheckListBox");
}

MfcRuntimeClassCompat* GetDragListBoxRuntimeClass() {
    return SimpleRuntimeClass<MfcDragListBoxCompat>("CDragListBox");
}

MfcRuntimeClassCompat* GetSpinButtonCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcSpinButtonCtrlCompat>("CSpinButtonCtrl");
}

MfcRuntimeClassCompat* GetSliderCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcSliderCtrlCompat>("CSliderCtrl");
}

MfcRuntimeClassCompat* GetProgressCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcProgressCtrlCompat>("CProgressCtrl");
}

MfcRuntimeClassCompat* GetComboBoxExRuntimeClass() {
    return SimpleRuntimeClass<MfcComboBoxExRuntimeCompat>("CComboBoxEx");
}

MfcRuntimeClassCompat* GetHeaderCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcHeaderCtrlCompat>("CHeaderCtrl");
}

MfcRuntimeClassCompat* GetHotKeyCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcHotKeyCtrlCompat>("CHotKeyCtrl");
}

MfcRuntimeClassCompat* GetAnimateCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcAnimateCtrlCompat>("CAnimateCtrl");
}

MfcRuntimeClassCompat* GetTabCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcTabCtrlCompat>("CTabCtrl");
}

MfcRuntimeClassCompat* GetTreeCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcTreeCtrlCompat>("CTreeCtrl");
}

MfcRuntimeClassCompat* GetListCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcListCtrlCompat>("CListCtrl");
}

MfcRuntimeClassCompat* GetToolBarCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcToolbarCtrlCompat>("CToolBarCtrl");
}

MfcRuntimeClassCompat* GetStatusBarCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcStatusBarCtrlCompat>("CStatusBarCtrl");
}

MfcRuntimeClassCompat* GetImageListRuntimeClass() {
    return SimpleRuntimeClass<MfcImageListCompat>("CImageList");
}

MfcRuntimeClassCompat* GetTempImageListRuntimeClass() {
    return SimpleRuntimeClass<MfcTempImageListRuntimeCompat>("CTempImageList");
}

MfcRuntimeClassCompat* GetRichEditCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcRichEditCtrlCompat>("CRichEditCtrl");
}

MfcRuntimeClassCompat* GetToolTipCtrlRuntimeClass() {
    return SimpleRuntimeClass<MfcToolTipCtrlCompat>("CToolTipCtrl");
}

void MfcCommonCtlRuntime_005eeb94(MfcWinAppCompat& app) {
    WinAppAssertValid(app);
    if (app.recent_file_list != nullptr) {
        RecentFileListWriteList(*app.recent_file_list);
    }
    if (app.preview_pages != 0) {
        WinAppWriteProfileInt(app, "Settings", "PreviewPages",
            app.preview_pages);
    }
}

int MfcCommonCtlRuntime_005eebfd(MfcWinAppCompat& app) {
    MfcCommonCtlRuntime_005eeb94(app);
    return app.quit_count;
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005eec82() {
    return GetWinAppRuntimeClass();
}

void MfcCommonCtlRuntime_005eeca1() {}

void MfcCommonCtlRuntime_005eed60(void* slot) {
    MfcThreadSlotRuntime_005eb878(static_cast<int*>(slot));
}

void MfcCommonCtlRuntime_005eecb8() {
    MfcCommonCtlRuntime_005eed60(nullptr);
}

void MfcCommonCtlRuntime_005eeca6() {
    CrtAtexit(MfcCommonCtlRuntime_005eecb8);
}

void MfcCommonCtlRuntime_005eec92() {
    MfcCommonCtlRuntime_005eeca1();
    MfcCommonCtlRuntime_005eeca6();
}

void MfcGdiObjectRuntimeTail_005ffe50(MfcGdiObjectCompat& object);

MfcGdiObjectCompat* MfcCommonCtlRuntime_005ee0f0(
    MfcGdiObjectCompat* object) {
    if (object != nullptr) {
        ConstructGdiObjectCompat(*object);
        object->runtime_class = GetTempGdiObjectRuntimeClassCompat();
        object->temporary = true;
    }
    return object;
}

MfcGdiObjectCompat* MfcCommonCtlRuntime_005eecd0(
    MfcGdiObjectCompat* object, unsigned flags) {
    if (object == nullptr) {
        return nullptr;
    }
    MfcGdiObjectRuntimeTail_005ffe50(*object);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(object));
    }
    return object;
}

MfcWinAppCompat* MfcCommonCtlRuntime_005eed00(
    MfcWinAppCompat* app, unsigned flags) {
    if (app == nullptr) {
        return nullptr;
    }
    MfcWinAppThreadRuntime_005ee8f1(app);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(app);
    }
    return app;
}

MfcCommandLineInfoRuntimeCompat* MfcCommonCtlRuntime_005eed30(
    MfcCommandLineInfoRuntimeCompat* info, unsigned flags) {
    if (info == nullptr) {
        return nullptr;
    }
    MfcWinAppThreadRuntime_005ee5fe(info);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(info);
    }
    return info;
}

bool MfcCommonCtlRuntime_005eed80() {
    CPINFO info{};
    return GetCPInfo(GetOEMCP(), &info) != FALSE && info.MaxCharSize > 1;
}

void MfcCommonCtlRuntime_005eedb1() {
    CommonCtlOemMbcsFlag() = MfcCommonCtlRuntime_005eed80();
}

void MfcCommonCtlRuntime_005eeda7() {
    MfcCommonCtlRuntime_005eedb1();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005eedc0() {
    return GetCheckListBoxRuntimeClass();
}

void MfcCommonCtlRuntime_005eeddf() {}

void MfcCommonCtlRuntime_005eee10(void* slot) {
    MfcThreadSlotRuntime_005eb878(static_cast<int*>(slot));
}

void MfcCommonCtlRuntime_005eedf6() {
    MfcCommonCtlRuntime_005eee10(nullptr);
}

void MfcCommonCtlRuntime_005eede4() {
    CrtAtexit(MfcCommonCtlRuntime_005eedf6);
}

void MfcCommonCtlRuntime_005eedd0() {
    MfcCommonCtlRuntime_005eeddf();
    MfcCommonCtlRuntime_005eede4();
}

MfcCheckListStateCompat* MfcCommonCtlRuntime_005eee50() {
    static MfcCheckListStateCompat check_state;
    if (check_state.runtime_class == nullptr) {
        ConstructAfxCheckListState(check_state);
    }
    return &check_state;
}

MfcCheckListStateCompat* MfcCommonCtlRuntime_005eee30() {
    return MfcCommonCtlRuntime_005eee50();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005eef81() {
    return GetFrameWndRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005eef91() {
    return GetViewRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005eefa1() {
    return GetControlBarRuntimeClass();
}

void MfcCommonCtlRuntime_005eefc0(int slot) {
    MfcThreadSlotRuntime_005eab60(slot);
}

void* MfcCommonCtlRuntime_005eefe0(int slot) {
    return MfcThreadSlotRuntime_005eabd3(slot);
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef000() {
    return GetColorDialogRuntimeClass();
}

void MfcCommonCtlRuntime_005ef01f() {}

void MfcCommonCtlRuntime_005ef050(void* slot) {
    MfcThreadSlotRuntime_005eb878(static_cast<int*>(slot));
}

void MfcCommonCtlRuntime_005ef036() {
    MfcCommonCtlRuntime_005ef050(nullptr);
}

void MfcCommonCtlRuntime_005ef024() {
    CrtAtexit(MfcCommonCtlRuntime_005ef036);
}

void MfcCommonCtlRuntime_005ef010() {
    MfcCommonCtlRuntime_005ef01f();
    MfcCommonCtlRuntime_005ef024();
}

void* MfcCommonCtlRuntime_005ef090() {
    return &GlobalMfcAuxData();
}

void* MfcCommonCtlRuntime_005ef070() {
    return MfcCommonCtlRuntime_005ef090();
}

void MfcCommonCtlRuntime_005ef15f() {}

void MfcCommonCtlRuntime_005ef3d0(void* slot) {
    MfcThreadSlotRuntime_005eb878(static_cast<int*>(slot));
}

void MfcCommonCtlRuntime_005ef176() {
    MfcCommonCtlRuntime_005ef3d0(nullptr);
}

void MfcCommonCtlRuntime_005ef164() {
    CrtAtexit(MfcCommonCtlRuntime_005ef176);
}

void MfcCommonCtlRuntime_005ef150() {
    MfcCommonCtlRuntime_005ef15f();
    MfcCommonCtlRuntime_005ef164();
}

int CALLBACK MfcCommonCtlRuntime_005ef3b8(const LOGFONTA*, const TEXTMETRICA*,
    DWORD, LPARAM lparam) {
    if (lparam != 0) {
        *reinterpret_cast<int*>(lparam) = 1;
    }
    return 0;
}

bool MfcCommonCtlRuntime_005ef34c(LPCSTR face_name) {
    if (face_name == nullptr || *face_name == '\0') {
        return false;
    }
    LOGFONTA font{};
    lstrcpynA(font.lfFaceName, face_name, LF_FACESIZE);
    font.lfCharSet = DEFAULT_CHARSET;
    bool found = false;
    HDC dc = GetDC(nullptr);
    if (dc != nullptr) {
        EnumFontFamiliesExA(dc, &font, MfcCommonCtlRuntime_005ef3b8,
            reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, dc);
    }
    return found;
}

void* MfcCommonCtlRuntime_005ef3f0() {
    static MfcPropPageFontInfoCompat font_info;
    return &font_info;
}

bool MfcCommonCtlRuntime_005ef185(HGLOBAL, short* point_size,
    int wizard) {
    if (point_size == nullptr) {
        return false;
    }
    auto* info = static_cast<MfcPropPageFontInfoCompat*>(
        MfcCommonCtlRuntime_005ef3f0());
    if (info->face_name.empty()) {
        info->face_name = wizard != 0 &&
                MfcCommonCtlRuntime_005ef34c("MS UI Gothic")
            ? "MS UI Gothic" : "MS Shell Dlg";
        info->point_size = 8;
    }
    *point_size = info->point_size;
    return true;
}

MfcPropPageFontInfoCompat* MfcCommonCtlRuntime_005ef4b0(
    MfcPropPageFontInfoCompat* info) {
    if (info != nullptr) {
        info->face_name.clear();
        info->point_size = 0;
    }
    return info;
}

MfcPropPageFontInfoCompat* MfcCommonCtlRuntime_005ef4f0(
    MfcPropPageFontInfoCompat* info, unsigned flags) {
    if (info == nullptr) {
        return nullptr;
    }
    DestroyPropPageFontInfoCompat(info);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(info));
    }
    return info;
}

void DestroyPropPageFontInfoCompat(void* info) {
    auto* font_info = static_cast<MfcPropPageFontInfoCompat*>(info);
    if (font_info == nullptr) {
        return;
    }
    font_info->face_name.clear();
    font_info->point_size = 0;
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef550() {
    return GetDragListBoxRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef560() {
    return GetSpinButtonCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef570() {
    return GetSliderCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef580() {
    return GetProgressCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef590() {
    return GetComboBoxExRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef5a0() {
    return GetHeaderCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef5b0() {
    return GetHotKeyCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef5c0() {
    return GetAnimateCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef5d0() {
    return GetTabCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef5e0() {
    return GetTreeCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef5f0() {
    return GetListCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef600() {
    return GetToolBarCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef610() {
    return GetStatusBarCtrlRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef68e() {
    return GetImageListRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntime_005ef70c() {
    return GetTempImageListRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntimeTail_005ef71c() {
    return GetRichEditCtrlRuntimeClass();
}

MfcImageListCompat& MfcCommonCtlRuntimeTail_005ef730(
    MfcImageListCompat& image_list) {
    image_list.handle = nullptr;
    image_list.owns_handle = true;
    return image_list;
}

void MfcCommonCtlRuntimeTail_005ef780(MfcImageListCompat& image_list) {
    if (image_list.handle != nullptr && image_list.owns_handle) {
        ImageList_Destroy(image_list.handle);
    }
    image_list.handle = nullptr;
    image_list.owns_handle = true;
}

MfcImageListCompat* MfcCommonCtlRuntimeTail_005ef750(
    MfcImageListCompat* image_list, unsigned flags) {
    if (image_list == nullptr) {
        return nullptr;
    }
    MfcCommonCtlRuntimeTail_005ef780(*image_list);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(image_list);
    }
    return image_list;
}

MfcRuntimeClassCompat* MfcCommonCtlRuntimeTail_005ef7a0() {
    return GetSplitterRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntimeTail_005ef7b0() {
    return GetCtrlViewRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntimeTail_005ef7c0() {
    return GetScrollViewRuntimeClass();
}

MfcRuntimeClassCompat* MfcCommonCtlRuntimeTail_005ef7d0() {
    return GetToolTipCtrlRuntimeClass();
}

void MfcCommonCtlRuntimeTail_005ef7e0(MfcWinAppCompat& app,
    const char* registry_key) {
    if (registry_key == nullptr || *registry_key == '\0') {
        return;
    }
    app.registry_key = registry_key;
    app.use_registry = true;
    if (app.profile_name.empty()) {
        app.profile_name = app.exe_name.empty() ? app.app_name : app.exe_name;
    }
}

void MfcCommonCtlRuntimeTail_005ef8b9(MfcWinAppCompat& app,
    UINT registry_key_id) {
    char key[256]{};
    if (LoadStringA(AfxGetResourceHandleCompat(), registry_key_id, key,
            static_cast<int>(sizeof(key))) > 0) {
        MfcCommonCtlRuntimeTail_005ef7e0(app, key);
    }
}

HKEY MfcCommonCtlRuntimeTail_005ef93c(MfcWinAppCompat& app) {
    if (app.registry_key.empty() || app.profile_name.empty()) {
        return nullptr;
    }
    HKEY software = nullptr;
    HKEY company = nullptr;
    HKEY profile = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0,
            KEY_READ | KEY_WRITE, &software) == ERROR_SUCCESS &&
        RegCreateKeyExA(software, app.registry_key.c_str(), 0, nullptr, 0,
            KEY_READ | KEY_WRITE, nullptr, &company, nullptr) ==
            ERROR_SUCCESS) {
        RegCreateKeyExA(company, app.profile_name.c_str(), 0, nullptr, 0,
            KEY_READ | KEY_WRITE, nullptr, &profile, nullptr);
    }
    if (software != nullptr) {
        RegCloseKey(software);
    }
    if (company != nullptr) {
        RegCloseKey(company);
    }
    return profile;
}

HKEY MfcCommonCtlRuntimeTail_005efa42(MfcWinAppCompat& app,
    const char* section) {
    if (section == nullptr) {
        return nullptr;
    }
    HKEY profile = MfcCommonCtlRuntimeTail_005ef93c(app);
    if (profile == nullptr) {
        return nullptr;
    }
    HKEY key = nullptr;
    RegCreateKeyExA(profile, section, 0, nullptr, 0, KEY_READ | KEY_WRITE,
        nullptr, &key, nullptr);
    RegCloseKey(profile);
    return key;
}

UINT MfcCommonCtlRuntimeTail_005efac0(MfcWinAppCompat& app,
    const char* section, const char* entry, UINT default_value) {
    if (section == nullptr || entry == nullptr) {
        return default_value;
    }
    if (!app.use_registry) {
        return GetPrivateProfileIntA(section, entry, default_value,
            app.profile_name.empty() ? nullptr : app.profile_name.c_str());
    }
    HKEY key = MfcCommonCtlRuntimeTail_005efa42(app, section);
    if (key == nullptr) {
        return default_value;
    }
    DWORD type = 0;
    DWORD value = default_value;
    DWORD bytes = sizeof(value);
    if (RegQueryValueExA(key, entry, nullptr, &type,
            reinterpret_cast<LPBYTE>(&value), &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value)) {
        value = default_value;
    }
    RegCloseKey(key);
    return value;
}

std::string MfcCommonCtlRuntimeTail_005efbf8(MfcWinAppCompat& app,
    const char* section, const char* entry, const char* default_value) {
    if (section == nullptr || entry == nullptr) {
        return default_value == nullptr ? std::string{} : default_value;
    }
    if (!app.use_registry) {
        char buffer[4096]{};
        GetPrivateProfileStringA(section, entry,
            default_value == nullptr ? "" : default_value, buffer,
            static_cast<DWORD>(sizeof(buffer)),
            app.profile_name.empty() ? nullptr : app.profile_name.c_str());
        return buffer;
    }
    HKEY key = MfcCommonCtlRuntimeTail_005efa42(app, section);
    if (key == nullptr) {
        return default_value == nullptr ? std::string{} : default_value;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    std::string value = default_value == nullptr ? std::string{} : default_value;
    if (RegQueryValueExA(key, entry, nullptr, &type, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        type == REG_SZ && bytes != 0) {
        std::string buffer(bytes, '\0');
        if (RegQueryValueExA(key, entry, nullptr, &type,
                reinterpret_cast<LPBYTE>(buffer.data()), &bytes) ==
            ERROR_SUCCESS) {
            if (!buffer.empty() && buffer.back() == '\0') {
                buffer.pop_back();
            }
            value = buffer;
        }
    }
    RegCloseKey(key);
    return value;
}

bool MfcCommonCtlRuntimeTail_005efe93(MfcWinAppCompat& app,
    const char* section, const char* entry, BYTE** data, UINT* bytes) {
    if (section == nullptr || entry == nullptr || data == nullptr ||
        bytes == nullptr) {
        return false;
    }
    *data = nullptr;
    *bytes = 0;
    if (!app.use_registry) {
        std::string text = MfcCommonCtlRuntimeTail_005efbf8(app, section,
            entry, nullptr);
        if ((text.size() % 2U) != 0U) {
            return false;
        }
        *bytes = static_cast<UINT>(text.size() / 2U);
        *data = new BYTE[*bytes];
        for (UINT index = 0; index < *bytes; ++index) {
            char pair[3]{text[index * 2U], text[index * 2U + 1U], 0};
            (*data)[index] = static_cast<BYTE>(std::strtoul(pair, nullptr, 16));
        }
        return true;
    }
    HKEY key = MfcCommonCtlRuntimeTail_005efa42(app, section);
    if (key == nullptr) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExA(key, entry, nullptr, &type, nullptr, &size);
    if (result == ERROR_SUCCESS && type == REG_BINARY && size != 0) {
        auto* buffer = new BYTE[size];
        result = RegQueryValueExA(key, entry, nullptr, &type, buffer, &size);
        if (result == ERROR_SUCCESS) {
            *data = buffer;
            *bytes = size;
        } else {
            delete[] buffer;
        }
    }
    RegCloseKey(key);
    return *data != nullptr;
}

MfcBaseModuleStateCompat* MfcAuxAppRuntime_005e98e0();
void* MfcAuxAppRuntime_005e9870();
void MfcAuxAppRuntime_005e9b76(MfcOleMessageFilterCompat& filter);
MfcOleStateCompat* MfcAuxAppRuntime_005e9ce0();
MfcOleStateCompat* MfcAuxAppRuntime_005e9d30();
void MfcModuleStateRuntime_005e8ff9(MfcAuxModuleStateCompat* state);
void MfcModuleStateRuntime_005e921a(MfcBaseModuleStateCompat* state);
void MfcModuleStateRuntime_005e93c5(MfcAuxModuleStateCompat* state);
void MfcThreadLocalRuntime_005ec200(MfcOleStateCompat* state);
void MfcThreadLocalRuntime_005ec270(MfcAmbientCacheCompat* cache);
void MfcAuxAppRuntime_005e9a70(MfcBaseModuleStateCompat& state);
void MfcAuxAppRuntime_005ea050(MfcCWndCompat& window);
MfcAmbientCacheCompat* MfcAuxAppRuntime_005ea0a0();
void MfcAuxAppRuntime_005ea2b0(MfcMenuCompat& menu);
void MfcAuxAppRuntime_005ea2df();
void MfcAuxAppRuntime_005ea2ee();
void MfcAuxAppRuntime_005ea300();
MfcAuxDataCompat* MfcAuxAppRuntime_005ea370(
    MfcAuxDataCompat* aux_data = nullptr);

namespace {

void ReleaseAuxUnknown(IUnknown*& unknown) {
    if (unknown != nullptr) {
        unknown->Release();
        unknown = nullptr;
    }
}

MfcRuntimeClassCompat* GetMenuRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CMenu", static_cast<int>(sizeof(MfcMenuCompat)), 0xffff,
        +[]() -> void* {
            auto* menu = new MfcMenuCompat();
            ConstructMenuCompat(*menu);
            menu->runtime_class = GetMenuRuntimeClassCompat();
            return menu;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetTempWndRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CTempWnd", static_cast<int>(sizeof(MfcTempWndRuntimeCompat)), 0xffff,
        nullptr, GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetTempMenuRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CTempMenu", static_cast<int>(sizeof(MfcTempMenuRuntimeCompat)),
        0xffff, nullptr, GetMenuRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetMapPtrToPtrRuntimeClassCompat() {
    return SimpleRuntimeClass<MfcMapPtrToPtrCompat>("CMapPtrToPtr");
}

MfcAuxModuleStateCompat& GlobalMfcAuxModuleState() {
    static MfcAuxModuleStateCompat state;
    return state;
}

MfcBaseModuleStateCompat& GlobalMfcBaseModuleState() {
    static MfcBaseModuleStateCompat state;
    return state;
}

MfcOleStateCompat& GlobalMfcOleState() {
    static MfcOleStateCompat state;
    return state;
}

MfcOleStateCompat& GlobalMfcRichEditState() {
    static MfcOleStateCompat state;
    return state;
}

MfcAmbientCacheCompat& GlobalMfcAmbientCache() {
    static MfcAmbientCacheCompat cache;
    return cache;
}

MfcThreadLocalObjectCompat& GlobalMfcThreadLocalObject() {
    static MfcThreadLocalObjectCompat local;
    return local;
}

void DestroyAuxModuleState(MfcAuxModuleStateCompat& state) {
    state.simple_lists.clear();
    ReleaseAuxUnknown(state.secondary_object);
    ReleaseAuxUnknown(state.primary_object);
    state.reference_count = 0;
    state.runtime_class = GetCObjectRuntimeClass();
}

void DestroyAuxOleState(MfcOleStateCompat& state) {
    if (state.cleanup != nullptr) {
        state.cleanup(state.cleanup_context);
    }
    if (state.library != nullptr) {
        FreeLibrary(state.library);
    }
    state.cleanup = nullptr;
    state.cleanup_context = nullptr;
    state.library = nullptr;
    state.runtime_class = GetCObjectRuntimeClass();
}

void DestroyAuxRichEditState(MfcOleStateCompat& state) {
    if (state.on_term != nullptr) {
        state.on_term();
    }
    state.on_term = nullptr;
    state.runtime_class = GetCObjectRuntimeClass();
}

void DestroyAuxWinThread(MfcWinThreadCompat& thread) {
#ifdef _WIN32
    if (thread.thread != nullptr && thread.thread != GetCurrentThread()) {
        CloseHandle(thread.thread);
    }
#endif
    thread.thread = nullptr;
    thread.thread_id = 0;
    thread.thread_params = nullptr;
    thread.thread_proc = nullptr;
    thread.main_window = nullptr;
    thread.active_window = nullptr;
    thread.ole_object_count = 0;
    thread.ole_user_control = false;
    thread.ole_post_quit_disabled = false;
    thread.ole_term_or_free_lib = nullptr;
    thread.runtime_class = GetCObjectRuntimeClass();
}

} // namespace

void MfcAuxAppRuntime_005e9654() {
    MfcAuxAppRuntime_005e98e0();
    MfcAuxAppRuntime_005e9870();
}

void MfcAuxAppRuntime_005e966b(MfcAuxModuleStateCompat& state) {
    if (state.reference_count < 1) {
        AfxTraceOutput("afxstate.cpp(0x106): module-state release underflow.\n");
        return;
    }
    if (InterlockedDecrement(&state.reference_count) == 0) {
        ReleaseAuxUnknown(state.secondary_object);
        ReleaseAuxUnknown(state.primary_object);
    }
}

MfcAuxModuleStateCompat* MfcAuxAppRuntime_005e9700(
    MfcAuxModuleStateCompat* state, unsigned flags) {
    if (state == nullptr) {
        return nullptr;
    }
    MfcModuleStateRuntime_005e8ff9(state);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(state));
    }
    return state;
}

void MfcAuxAppRuntime_005e9730(MfcThreadLocalObjectCompat* local) {
    if (local != nullptr) {
        local->data = nullptr;
    }
}

MfcBaseModuleStateCompat* MfcAuxAppRuntime_005e9750(
    MfcBaseModuleStateCompat* state, unsigned flags) {
    if (state == nullptr) {
        return nullptr;
    }
    MfcModuleStateRuntime_005e921a(state);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(state));
    }
    return state;
}

void MfcAuxAppRuntime_005e9780(MfcThreadLocalObjectCompat* local) {
    MfcAuxAppRuntime_005e9730(local);
}

MfcAuxModuleStateCompat* MfcAuxAppRuntime_005e97a0(
    MfcAuxModuleStateCompat* state, unsigned flags) {
    if (state == nullptr) {
        return nullptr;
    }
    MfcModuleStateRuntime_005e93c5(state);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(state));
    }
    return state;
}

void MfcAuxAppRuntime_005e97d0(MfcThreadLocalObjectCompat* local) {
    MfcAuxAppRuntime_005e9730(local);
}

MfcSimpleListCompat* MfcAuxAppRuntime_005e97f0(
    MfcSimpleListCompat* list, int element_size) {
    if (list != nullptr) {
        list->element_size = element_size;
        list->head = nullptr;
    }
    return list;
}

MfcSimpleListCompat* MfcAuxAppRuntime_005e9810(
    MfcSimpleListCompat* list, int element_size) {
    return MfcAuxAppRuntime_005e97f0(list, element_size);
}

MfcSimpleListCompat* MfcAuxAppRuntime_005e9830(
    MfcSimpleListCompat* list, int element_size) {
    return MfcAuxAppRuntime_005e97f0(list, element_size);
}

MfcSimpleListCompat* MfcAuxAppRuntime_005e9850(
    MfcSimpleListCompat* list, int element_size) {
    return MfcAuxAppRuntime_005e97f0(list, element_size);
}

template <typename T>
T* AssertMfcAuxLocalObject(T* object, int line) {
    if (object == nullptr && AfxAssertFailedLine("afxstat_.h", line)) {
        CrtDebugBreak();
    }
    return object;
}

void* MfcAuxAppRuntime_005e9870() {
    MfcThreadLocalObjectCompat& local = GlobalMfcThreadLocalObject();
    if (local.data == nullptr) {
        local.data = &GlobalMfcAuxModuleState();
    }
    return AssertMfcAuxLocalObject(local.data, 0xae);
}

int MfcAuxAppRuntime_005e98c0(void* context = nullptr) {
    return MfcExceptionRuntimeThunk_005e8f00(context);
}

MfcBaseModuleStateCompat* MfcAuxAppRuntime_005e98e0() {
    MfcBaseModuleStateCompat& state = GlobalMfcBaseModuleState();
    if (state.runtime_class == nullptr) {
        state.runtime_class = GetCObjectRuntimeClass();
        state.reference_count = 1;
        state.ole_enabled = true;
    }
    return AssertMfcAuxLocalObject(&state, 0xce);
}

MfcBaseModuleStateCompat* MfcAuxAppRuntime_005e9a40(
    MfcBaseModuleStateCompat* state, unsigned flags) {
    if (state == nullptr) {
        return nullptr;
    }
    MfcAuxAppRuntime_005e9a70(*state);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(state));
    }
    return state;
}

void MfcAuxAppRuntime_005e9a70(MfcBaseModuleStateCompat& state) {
    MfcModuleStateRuntime_005e921a(&state);
}

MfcOleMessageFilterCompat& MfcAuxAppRuntime_005e9a90(
    MfcOleMessageFilterCompat& filter, DWORD retry_reply,
    DWORD busy_reply) {
    ConstructCmdTarget(filter);
    filter.runtime_class = GetCmdTargetRuntimeClass();
    filter.retry_reply = retry_reply;
    filter.busy_reply = busy_reply;
    MfcAuxAppRuntime_005e9b76(filter);
    return filter;
}

MfcOleMessageFilterCompat& MfcAuxAppRuntime_005e9b03(
    MfcOleMessageFilterCompat& filter) {
    return MfcAuxAppRuntime_005e9a90(filter, 0, 0);
}

void MfcAuxAppRuntime_005e9b76(MfcOleMessageFilterCompat& filter) {
    filter.dispatch_map = nullptr;
    filter.connection_map = nullptr;
    filter.busy_timeout = 0;
    filter.retry_timeout = 0;
    filter.message_pending_delay = 0;
    filter.flags = 0;
    filter.reserved = 0;
    GetCursorPos(&filter.last_cursor);
    filter.message_pending = 1;
    filter.registered = 0;
    filter.last_tick = 0;
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005e9bf2() {
    return GetCWinThreadRuntimeClass();
}

MfcWinThreadCompat* MfcAuxAppRuntime_005e9c10(
    MfcWinThreadCompat* thread, unsigned flags) {
    if (thread == nullptr) {
        return nullptr;
    }
    DestroyAuxWinThread(*thread);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(thread);
    }
    return thread;
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005e9c40(
    const MfcObjectCompat* object) {
    return object == nullptr || object->runtime_class == nullptr
        ? GetCObjectRuntimeClass() : object->runtime_class;
}

void* MfcAuxAppRuntime_005e9c60() {
    return MfcAuxAppRuntime_005e9870();
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9ca0() {
    return MfcAuxAppRuntime_005e9ce0();
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9cc0() {
    return MfcAuxAppRuntime_005e9d30();
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9ce0() {
    MfcOleStateCompat& state = GlobalMfcOleState();
    if (state.runtime_class == nullptr) {
        state.runtime_class = GetCObjectRuntimeClass();
    }
    return AssertMfcAuxLocalObject(&state, 0xce);
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9d30() {
    MfcOleStateCompat& state = GlobalMfcRichEditState();
    if (state.runtime_class == nullptr) {
        state.runtime_class = GetCObjectRuntimeClass();
    }
    return AssertMfcAuxLocalObject(&state, 0xae);
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9e60(MfcOleStateCompat* state) {
    if (state != nullptr) {
        ConstructCObject(*state);
        state->library = nullptr;
        state->cleanup = nullptr;
        state->cleanup_context = nullptr;
        state->on_term = nullptr;
    }
    return state;
}

MfcAmbientCacheCompat* MfcAuxAppRuntime_005e9e80(
    MfcAmbientCacheCompat* cache) {
    if (cache != nullptr) {
        ConstructCObject(*cache);
        cache->cached_object = nullptr;
    }
    return cache;
}

MfcOleStateCompat* MfcAuxAppRuntime_005e9ea0(
    MfcOleStateCompat* state, unsigned flags) {
    if (state == nullptr) {
        return nullptr;
    }
    MfcThreadLocalRuntime_005ec200(state);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(state));
    }
    return state;
}

MfcAmbientCacheCompat* MfcAuxAppRuntime_005e9ed0(
    MfcAmbientCacheCompat* cache, unsigned flags) {
    if (cache == nullptr) {
        return nullptr;
    }
    MfcThreadLocalRuntime_005ec270(cache);
    if ((flags & 1U) != 0U) {
        MfcThreadSlotRuntime_005ead15(reinterpret_cast<HLOCAL>(cache));
    }
    return cache;
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005e9f6e() {
    return GetCWndRuntimeClass();
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005e9fec() {
    return GetTempWndRuntimeClassCompat();
}

MfcTempWndRuntimeCompat* MfcAuxAppRuntime_005ea000(
    MfcTempWndRuntimeCompat* window) {
    if (window != nullptr) {
        ConstructCWnd(*window);
        window->runtime_class = GetTempWndRuntimeClassCompat();
        window->temporary = true;
    }
    return window;
}

MfcTempWndRuntimeCompat* MfcAuxAppRuntime_005ea020(
    MfcTempWndRuntimeCompat* window, unsigned flags) {
    if (window == nullptr) {
        return nullptr;
    }
    MfcAuxAppRuntime_005ea050(*window);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(window);
    }
    return window;
}

void MfcAuxAppRuntime_005ea050(MfcCWndCompat& window) {
    DestroyCWndCompat(window);
}

MfcAmbientCacheCompat* MfcAuxAppRuntime_005ea070() {
    return MfcAuxAppRuntime_005ea0a0();
}

void* MfcAuxAppRuntime_005ea090(void** slot) {
    return slot == nullptr ? nullptr : *slot;
}

MfcAmbientCacheCompat* MfcAuxAppRuntime_005ea0a0() {
    MfcAmbientCacheCompat& cache = GlobalMfcAmbientCache();
    if (cache.runtime_class == nullptr) {
        cache.runtime_class = GetCObjectRuntimeClass();
    }
    return AssertMfcAuxLocalObject(&cache, 0xce);
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005ea1ce() {
    return GetMenuRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005ea24c() {
    return GetTempMenuRuntimeClassCompat();
}

MfcTempMenuRuntimeCompat* MfcAuxAppRuntime_005ea260(
    MfcTempMenuRuntimeCompat* menu) {
    if (menu != nullptr) {
        ConstructMenuCompat(*menu);
        menu->runtime_class = GetTempMenuRuntimeClassCompat();
        menu->temporary = true;
    }
    return menu;
}

MfcTempMenuRuntimeCompat* MfcAuxAppRuntime_005ea280(
    MfcTempMenuRuntimeCompat* menu, unsigned flags) {
    if (menu == nullptr) {
        return nullptr;
    }
    MfcAuxAppRuntime_005ea2b0(*menu);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(menu);
    }
    return menu;
}

void MfcAuxAppRuntime_005ea2b0(MfcMenuCompat& menu) {
    DestroyMenuCompat(menu);
}

void MfcAuxAppRuntime_005ea2d0() {
    MfcAuxAppRuntime_005ea2df();
    MfcAuxAppRuntime_005ea2ee();
}

void MfcAuxAppRuntime_005ea2df() {
    MfcAuxAppRuntime_005ea370(&GlobalMfcAuxData());
}

void MfcAuxAppRuntime_005ea2ee() {
    CrtAtexit(MfcAuxAppRuntime_005ea300);
}

void MfcAuxAppRuntime_005ea300() {
    MfcAuxDataCompat& aux_data = GlobalMfcAuxData();
    aux_data.wait_cursor = nullptr;
    aux_data.arrow_cursor = nullptr;
    aux_data.button_face_brush = nullptr;
    aux_data.window_frame_brush = nullptr;
    aux_data.metrics_initialized = false;
}

void MfcAuxAppRuntime_005ea30f() {
    MfcAuxDataCompat& aux_data = GlobalMfcAuxData();
    if (aux_data.win4_or_later) {
        aux_data.scroll_width = GetSystemMetrics(SM_CXVSCROLL) + 1;
        aux_data.scroll_height = GetSystemMetrics(SM_CYHSCROLL) + 1;
        aux_data.border_adjusted_for_3d = true;
    }
}

void MfcAuxAppRuntime_005ea347() {
    MfcAuxDataCompat& aux_data = GlobalMfcAuxData();
    aux_data.scroll_width = GetSystemMetrics(SM_CXVSCROLL);
    aux_data.scroll_height = GetSystemMetrics(SM_CYHSCROLL);
    aux_data.border_adjusted_for_3d = false;
}

MfcAuxDataCompat* MfcAuxAppRuntime_005ea370(MfcAuxDataCompat* aux_data) {
    if (aux_data == nullptr) {
        aux_data = &GlobalMfcAuxData();
    }
    const DWORD version = GetVersion();
    const DWORD major = version & 0xffU;
    const DWORD minor = (version >> 8) & 0xffU;
    aux_data->windows_version = major * 0x100U + minor;
    aux_data->win32s_platform = (version & 0x80000000U) != 0U;
    aux_data->win4_or_later = major > 3U;
    aux_data->win31_compat = !aux_data->win4_or_later;
    aux_data->process_uses_win4 = false;
    if (aux_data->win4_or_later) {
        aux_data->process_uses_win4 = GetProcessVersion(0) > 0x0003ffffU;
    }
    UpdateMfcAuxDataSysMetrics(*aux_data);
    UpdateMfcAuxDataSysColors(*aux_data);
    aux_data->wait_cursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32514));
    aux_data->arrow_cursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512));
    MfcAuxAppRuntime_005ea347();
    return aux_data;
}

MfcRuntimeClassCompat* MfcAuxAppRuntime_005ea4f0() {
    return GetMapPtrToPtrRuntimeClassCompat();
}

namespace {

struct MfcThreadStateRuntimeCompat {
    MfcObjectCompat object;
    MfcBaseModuleStateCompat* module_state = nullptr;
    MfcCWndCompat* current_window = nullptr;
    MfcCWndCompat* routing_frame = nullptr;
    void* message_filter = nullptr;
    bool initialized = false;
};

MfcThreadStateRuntimeCompat& GlobalMfcThreadStateRuntime() {
    static MfcThreadStateRuntimeCompat state;
    return state;
}

void InitializeThreadStateRuntime(MfcThreadStateRuntimeCompat& state) {
    if (!state.initialized) {
        state.object.runtime_class = GetCObjectRuntimeClass();
        state.module_state = MfcAuxAppRuntime_005e98e0();
        state.current_window =
            static_cast<MfcCWndCompat*>(GetThreadStateCurrentWindowSlot());
        state.routing_frame =
            static_cast<MfcCWndCompat*>(GetThreadStateRoutingFrameSlot());
        state.initialized = true;
    }
}

void ResetThreadStateRuntime(MfcThreadStateRuntimeCompat& state) {
    state.module_state = nullptr;
    state.current_window = nullptr;
    state.routing_frame = nullptr;
    state.message_filter = nullptr;
    state.initialized = false;
    state.object.runtime_class = GetCObjectRuntimeClass();
}

} // namespace

void MfcModuleStateRuntime_005e8ff9(
    MfcAuxModuleStateCompat* state = nullptr) {
    if (state == nullptr) {
        state = &GlobalMfcAuxModuleState();
    }
    DestroyAuxModuleState(*state);
}

MfcThreadStateRuntimeCompat* MfcModuleStateRuntime_005e9105(
    void* = nullptr) {
    MfcExceptionRuntimeThunk_005e8f00();
    MfcThreadStateRuntimeCompat& state = GlobalMfcThreadStateRuntime();
    InitializeThreadStateRuntime(state);
    return &state;
}

void MfcModuleStateRuntime_005e9123() {
}

void MfcModuleStateRuntime_005e913a() {
    ResetThreadStateRuntime(GlobalMfcThreadStateRuntime());
    MfcAuxAppRuntime_005e9730(&GlobalMfcThreadLocalObject());
}

void MfcModuleStateRuntime_005e9128() {
    CrtAtexit(MfcModuleStateRuntime_005e913a);
}

void MfcModuleStateRuntime_005e9114() {
    MfcModuleStateRuntime_005e9123();
    MfcModuleStateRuntime_005e9128();
}

MfcBaseModuleStateCompat* MfcModuleStateRuntime_005e9149(
    MfcBaseModuleStateCompat* state = nullptr, bool ole_enabled = true) {
    if (state == nullptr) {
        state = &GlobalMfcBaseModuleState();
    }
    state->runtime_class = GetCObjectRuntimeClass();
    state->reference_count = 1;
    state->ole_enabled = ole_enabled;
    state->occ_manager = nullptr;
    state->simple_lists.clear();
    state->simple_lists.push_back(MfcSimpleListCompat{0x1c, nullptr});
    state->simple_lists.push_back(MfcSimpleListCompat{0x14, nullptr});
    state->simple_lists.push_back(MfcSimpleListCompat{0x18, nullptr});
    return state;
}

void InitializeMfcBaseModuleStateCompat(bool ole_enabled) {
    MfcModuleStateRuntime_005e9149(nullptr, ole_enabled);
}

void MfcModuleStateRuntime_005e921a(
    MfcBaseModuleStateCompat* state = nullptr) {
    if (state == nullptr) {
        state = &GlobalMfcBaseModuleState();
    }
    DestroyAuxModuleState(*state);
    state->ole_enabled = false;
    state->occ_manager = nullptr;
    if (GlobalMfcThreadStateRuntime().module_state == state) {
        GlobalMfcThreadStateRuntime().module_state = nullptr;
    }
}

MfcAuxModuleStateCompat* MfcModuleStateRuntime_005e9355(
    MfcAuxModuleStateCompat* state = nullptr) {
    if (state == nullptr) {
        state = &GlobalMfcAuxModuleState();
    }
    state->runtime_class = GetCObjectRuntimeClass();
    state->reference_count = 1;
    state->simple_lists.clear();
    state->simple_lists.push_back(MfcSimpleListCompat{0x54, nullptr});
    return state;
}

void MfcModuleStateRuntime_005e93c5(
    MfcAuxModuleStateCompat* state = nullptr) {
    if (state == nullptr) {
        state = &GlobalMfcAuxModuleState();
    }
    DestroyAuxModuleState(*state);
}

void MfcModuleStateRuntime_005e95be() {
}

void MfcModuleStateRuntime_005e95d5() {
    ResetThreadStateRuntime(GlobalMfcThreadStateRuntime());
    MfcAuxAppRuntime_005e97d0(&GlobalMfcThreadLocalObject());
}

void MfcModuleStateRuntime_005e95c3() {
    CrtAtexit(MfcModuleStateRuntime_005e95d5);
}

void MfcModuleStateRuntime_005e95af() {
    MfcModuleStateRuntime_005e95be();
    MfcModuleStateRuntime_005e95c3();
}

void MfcModuleStateRuntime_005e95e4() {
    GlobalMfcThreadStateRuntime().module_state = MfcAuxAppRuntime_005e98e0();
    GlobalMfcThreadStateRuntime().initialized = true;
}

MfcBaseModuleStateCompat* MfcModuleStateRuntime_005e95f3(
    void* = nullptr, void* = nullptr, void* = nullptr, void* = nullptr) {
    MfcThreadStateRuntimeCompat* thread_state =
        MfcModuleStateRuntime_005e9105();
    if (thread_state->module_state == nullptr) {
        thread_state->module_state = MfcAuxAppRuntime_005e98e0();
    }
    return thread_state->module_state;
}

namespace {

enum class MfcShellCommandRuntime {
    FileNew = 0,
    FileOpen = 1,
    FilePrint = 2,
    FilePrintTo = 3,
    FileDde = 4,
    AppRegister = 5,
    AppUnregister = 6,
};

struct MfcCommandLineInfoRuntimeCompat : MfcObjectCompat {
    bool show_splash = true;
    bool run_embedded = false;
    bool run_automated = false;
    MfcShellCommandRuntime shell_command = MfcShellCommandRuntime::FileNew;
    std::string file_name;
    std::string printer_name;
    std::string driver_name;
    std::string port_name;
};

template <typename Type>
MfcRuntimeClassCompat* ControlRuntimeClass(const char* class_name) {
    static MfcRuntimeClassCompat runtime_class{
        class_name, static_cast<int>(sizeof(Type)), 0xffff,
        +[]() -> void* { return new Type(); },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetBitmapButtonRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "CBitmapButton", static_cast<int>(sizeof(MfcBitmapButtonCompat)),
        0xffff,
        +[]() -> void* {
            auto* button = new MfcBitmapButtonCompat();
            ConstructMfcBitmapButton(*button);
            return button;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcRuntimeClassCompat* GetStaticRuntimeClassCompat() {
    return ControlRuntimeClass<MfcStaticCompat>("CStatic");
}

MfcRuntimeClassCompat* GetButtonRuntimeClassCompat() {
    return ControlRuntimeClass<MfcButtonCompat>("CButton");
}

MfcRuntimeClassCompat* GetListBoxRuntimeClassCompat() {
    return ControlRuntimeClass<MfcListBoxCompat>("CListBox");
}

MfcRuntimeClassCompat* GetComboBoxRuntimeClassCompat() {
    return ControlRuntimeClass<MfcComboBoxCompat>("CComboBox");
}

MfcRuntimeClassCompat* GetEditRuntimeClassCompat() {
    return ControlRuntimeClass<MfcEditCompat>("CEdit");
}

MfcRuntimeClassCompat* GetScrollBarRuntimeClassCompat() {
    return ControlRuntimeClass<MfcScrollBarCompat>("CScrollBar");
}

void ClearCommandLineInfoRuntime(MfcCommandLineInfoRuntimeCompat& info) {
    info.file_name.clear();
    info.printer_name.clear();
    info.driver_name.clear();
    info.port_name.clear();
    info.run_embedded = false;
    info.run_automated = false;
    info.show_splash = true;
    info.shell_command = MfcShellCommandRuntime::FileNew;
}

bool IsCommandSwitchText(const std::string& argument) {
    return argument.size() > 1 &&
        (argument.front() == '-' || argument.front() == '/');
}

const char* SkipCommandSwitchPrefix(const std::string& argument) {
    return IsCommandSwitchText(argument) ? argument.c_str() + 1
                                         : argument.c_str();
}

} // namespace

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee00f() {
    return GetToolBarRuntimeClass();
}

MfcToolBarCompat* MfcWinAppThreadRuntime_005ee020(
    MfcToolBarCompat* toolbar, unsigned flags) {
    if (toolbar == nullptr) {
        return nullptr;
    }
    DestroyControlBar(*toolbar);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(toolbar);
    }
    return toolbar;
}

void* MfcWinAppThreadRuntime_005ee050(void* object) {
    return object == nullptr ? nullptr : static_cast<unsigned char*>(object) + 8;
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee070() {
    return GetDialogBarRuntimeClass();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee080() {
    return GetBitmapButtonRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee090() {
    return GetStaticRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee0a0() {
    return GetButtonRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee0b0() {
    return GetListBoxRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee0c0() {
    return GetComboBoxRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee0d0() {
    return GetEditRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcWinAppThreadRuntime_005ee0e0() {
    return GetScrollBarRuntimeClassCompat();
}

MfcWinAppCompat* MfcWinAppThreadRuntime_005ee10f(
    MfcWinAppCompat* app, const char* app_name = nullptr) {
    if (app == nullptr) {
        return nullptr;
    }
    ConstructWinApp(*app, app_name);
    g_current_win_thread = app;
    g_app_win_thread = app;
    MfcModuleStateRuntime_005e95e4();
    return app;
}

int MfcWinAppThreadRuntime_005ee33f(MfcWinAppCompat* app) {
    if (app != nullptr) {
        WinAppAssertValid(*app);
    }
    return 1;
}

int MfcWinAppThreadRuntime_005ee3ae() {
    return 1;
}

void MfcWinAppThreadRuntime_005ee3be(
    MfcWinAppCompat* app, int max_recent_files) {
    if (app == nullptr) {
        return;
    }
    if (app->recent_file_list != nullptr) {
        DeleteRecentFileListScalarDtor(app->recent_file_list, 1);
        app->recent_file_list = nullptr;
    }
    if (max_recent_files > 0) {
        auto* recent = new MfcRecentFileListCompat();
        ConstructRecentFileList(*recent, 0, "Recent File List", "File%d",
            max_recent_files, 0x1e);
        RecentFileListReadList(*recent);
        app->recent_file_list = recent;
    }
}

void MfcWinAppThreadRuntime_005ee6df(
    MfcCommandLineInfoRuntimeCompat* info, const char* flag_text) {
    if (info == nullptr || flag_text == nullptr) {
        return;
    }
    if (lstrcmpiA(flag_text, "Register") == 0 ||
        lstrcmpiA(flag_text, "Regserver") == 0) {
        info->shell_command = MfcShellCommandRuntime::AppRegister;
    } else if (lstrcmpiA(flag_text, "Unregister") == 0 ||
        lstrcmpiA(flag_text, "Unregserver") == 0) {
        info->shell_command = MfcShellCommandRuntime::AppUnregister;
    } else if (lstrcmpiA(flag_text, "Embedding") == 0) {
        AfxOleSetUserCtrl(false);
        info->run_embedded = true;
        info->show_splash = false;
    } else if (lstrcmpiA(flag_text, "Automation") == 0) {
        AfxOleSetUserCtrl(false);
        info->run_automated = true;
        info->show_splash = false;
    } else if (lstrcmpiA(flag_text, "dde") == 0) {
        info->shell_command = MfcShellCommandRuntime::FileDde;
    } else if (lstrcmpiA(flag_text, "pt") == 0) {
        info->shell_command = MfcShellCommandRuntime::FilePrintTo;
    } else if (lstrcmpiA(flag_text, "p") == 0) {
        info->shell_command = MfcShellCommandRuntime::FilePrint;
    }
}

void MfcWinAppThreadRuntime_005ee7e9(
    MfcCommandLineInfoRuntimeCompat* info, const char* value) {
    if (info == nullptr || value == nullptr) {
        return;
    }
    if (info->file_name.empty()) {
        info->file_name = value;
    } else if (info->printer_name.empty()) {
        info->printer_name = value;
    } else if (info->driver_name.empty()) {
        info->driver_name = value;
    } else if (info->port_name.empty()) {
        info->port_name = value;
    }
}

void MfcWinAppThreadRuntime_005ee88f(
    MfcCommandLineInfoRuntimeCompat* info, bool last) {
    if (info == nullptr || !last) {
        return;
    }
    if (info->shell_command == MfcShellCommandRuntime::FileNew &&
        !info->file_name.empty()) {
        info->shell_command = MfcShellCommandRuntime::FileOpen;
    }
    if (info->run_embedded || info->run_automated) {
        info->show_splash = false;
    }
}

void MfcWinAppThreadRuntime_005ee67f(
    MfcCommandLineInfoRuntimeCompat* info, const char* value, bool flag,
    bool last) {
    if (flag) {
        MfcWinAppThreadRuntime_005ee6df(info, value);
    } else {
        MfcWinAppThreadRuntime_005ee7e9(info, value);
    }
    MfcWinAppThreadRuntime_005ee88f(info, last);
}

void MfcWinAppThreadRuntime_005ee4be(
    MfcWinAppCompat* app, MfcCommandLineInfoRuntimeCompat* info) {
    if (info == nullptr) {
        return;
    }
    const char* command_line = app == nullptr ? nullptr : app->command_line.c_str();
    std::vector<std::string> arguments = InitializeArgumentVector(command_line);
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const bool flag = IsCommandSwitchText(arguments[index]);
        const bool last = index + 1 == arguments.size();
        MfcWinAppThreadRuntime_005ee67f(info,
            SkipCommandSwitchPrefix(arguments[index]), flag, last);
    }
}

MfcCommandLineInfoRuntimeCompat* MfcWinAppThreadRuntime_005ee552(
    MfcCommandLineInfoRuntimeCompat* info) {
    if (info != nullptr) {
        info->runtime_class = GetCObjectRuntimeClass();
        ClearCommandLineInfoRuntime(*info);
    }
    return info;
}

void MfcWinAppThreadRuntime_005ee5fe(
    MfcCommandLineInfoRuntimeCompat* info) {
    if (info != nullptr) {
        ClearCommandLineInfoRuntime(*info);
        info->runtime_class = GetCObjectRuntimeClass();
    }
}

void MfcWinAppThreadRuntime_005ee8f1(MfcWinAppCompat* app) {
    if (app == nullptr) {
        return;
    }
    if (app->recent_file_list != nullptr) {
        DeleteRecentFileListScalarDtor(app->recent_file_list, 1);
        app->recent_file_list = nullptr;
    }
    DestroyWinApp(*app);
}

namespace {

bool IsValidOleStatus(OleDateStatus status) {
    return status == OleDateStatus::Valid;
}

constexpr double kOleHalfSecondDays = 1.0 / (2.0 * 24.0 * 60.0 * 60.0);
constexpr double kOleHoursPerDay = 24.0;
constexpr double kOleMinutesPerHour = 60.0;

OleCurrencyCompat COleCurrency(LONG units, LONG fractional_10000) {
    return SetOleCurrencyParts(units, fractional_10000);
}

OleDateTimeCompat COleDateTime(int year, int month, int day, int hour,
    int minute, int second) {
    return ConstructOleDateTimeFromFields(static_cast<WORD>(year),
        static_cast<WORD>(month), static_cast<WORD>(day),
        static_cast<WORD>(hour), static_cast<WORD>(minute),
        static_cast<WORD>(second));
}

OleDateTimeCompat COleDateTime(WORD dos_date, WORD dos_time) {
    OleDateTimeCompat out{};
    if (DosDateTimeToVariantTime(dos_date, dos_time, &out.value) == FALSE) {
        out.value = 0.0;
        out.status = OleDateStatus::Invalid;
    } else {
        out.status = OleDateStatus::Valid;
    }
    return out;
}

void SetStatus(OleCurrencyCompat& value, OleDateStatus status) {
    value.status = status;
}

void ClearVariantWithAfxOleAssert(VARIANTARG* variant, int line) {
    const HRESULT hr = VariantClear(variant);
    if (hr != S_OK && AfxAssertFailedLine("afxole.inl", line)) {
        CrtDebugBreak();
    }
}

double ValidOleSpanValueWithAfxOleAssert(const OleDateTimeSpanCompat& span,
    int line) {
    if (!IsValidOleStatus(span.status) &&
        AfxAssertFailedLine("afxole.inl", line)) {
        CrtDebugBreak();
    }
    return span.days;
}

int OleSpanTotalUnitsWithAfxOleAssert(const OleDateTimeSpanCompat& span,
    double units_per_day, int line) {
    return static_cast<int>(
        ValidOleSpanValueWithAfxOleAssert(span, line) * units_per_day +
        kOleHalfSecondDays);
}

} // namespace

ULONG MfcOleValueRuntime_005f7414(MfcOleInnerUnknownCompat& inner);
ULONG MfcOleRuntime_005f6ec7(MfcOleInnerUnknownCompat& inner);
void MfcOleValueRuntime_005f816b(COleSafeArray* variant);
void MfcOleDispatchRuntime_005fa44b(MfcOleDispatchDriverCompat& driver);

MfcOleInnerUnknownCompat& OleInnerUnknownFromThis(void* inner_this) {
    return *reinterpret_cast<MfcOleInnerUnknownCompat*>(
        static_cast<unsigned char*>(inner_this) - 0x0c);
}

void* OleInnerUnknownThis(MfcOleInnerUnknownCompat& inner) {
    return inner.inner_unknown_adjustor.data() + 0x0c;
}

ULONG ReleaseOleInnerUnknownBody(MfcOleInnerUnknownCompat& inner) {
    if (inner.reference_count == 0) {
        return 0;
    }
    const ULONG remaining =
        static_cast<ULONG>(InterlockedDecrement(&inner.reference_count));
    if (remaining == 0 && inner.controlling_unknown != nullptr) {
        inner.controlling_unknown->Release();
        inner.controlling_unknown = nullptr;
    }
    return remaining;
}

HRESULT MfcOleValueRuntime_005f726a(MfcCommandTargetCompat&,
    IUnknown* outer_unknown, REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (outer_unknown != nullptr) {
        return outer_unknown->QueryInterface(iid, object);
    }
    (void)iid;
    return E_NOINTERFACE;
}

ULONG MfcOleValueRuntime_005f72ab(void* inner_this) {
    return MfcOleValueRuntime_005f7414(OleInnerUnknownFromThis(inner_this));
}

ULONG MfcOleValueRuntime_005f72c6(void* inner_this) {
    return MfcOleRuntime_005f6ec7(OleInnerUnknownFromThis(inner_this));
}

void MfcOleValueRuntime_005f732d(MfcOleInnerUnknownCompat& inner) {
    if (inner.reference_count == 0 || inner.controlling_unknown == nullptr) {
        return;
    }
    InterlockedIncrement(&inner.reference_count);
    CoDisconnectObject(inner.controlling_unknown, 0);
    inner.reference_count = 0;
}

MfcOleExceptionCompat* MfcOleValueRuntime_005f73f8(
    MfcOleExceptionCompat* exception) {
    if (exception != nullptr) {
        DestroySimpleException(*exception);
        exception->scode = S_OK;
    }
    return exception;
}

ULONG MfcOleValueRuntime_005f7414(MfcOleInnerUnknownCompat& inner) {
    if (inner.controlling_unknown != nullptr) {
        inner.controlling_unknown->AddRef();
    }
    return static_cast<ULONG>(InterlockedIncrement(&inner.reference_count));
}

bool MfcOleValueRuntime_005f7458(const MfcCommandTargetCompat& target) {
    AfxAssertValidObject(&target, "afxole.inl", 0x4f);
    return target.dispatch_map != nullptr;
}

void* MfcOleValueRuntime_005f747f(MfcCommandTargetCompat& target) {
    AfxAssertValidObject(&target, "afxole.inl", 0x51);
    return &target.routing_target;
}

void MfcOleValueRuntime_005f74a0(MfcOleDispatchDriverCompat& driver) {
    MfcOleDispatchRuntime_005fa44b(driver);
}

void* MfcOleValueRuntime_005f74b3(void** value) {
    return value == nullptr ? nullptr : *value;
}

COleVariant* MfcOleValueRuntime_005f74c3(COleVariant* variant) {
    InitializeOleVariant(*variant);
    return variant;
}

void MfcOleValueRuntime_005f74da(COleVariant* variant) {
    ClearVariantWithAfxOleAssert(variant, 0x5d);
}

void MfcOleValueRuntime_005f7510(COleVariant* variant) {
    ClearVariantWithAfxOleAssert(variant, 0x5f);
}

COleVariant* MfcOleValueRuntime_005f7620(COleVariant* variant,
    const OleDateTimeCompat* source) {
    V_VT(variant) = VT_DATE;
    V_DATE(variant) = source->value;
    return variant;
}

bool MfcOleValueRuntime_005f7691(const COleVariant& lhs,
    const COleVariant& rhs) {
    return CompareOleVariantsExact(lhs, rhs);
}

void* MfcOleValueRuntime_005f76aa(void* value) {
    return value;
}

void* MfcOleValueRuntime_005f76b8(void* value) {
    return value;
}

OleCurrencyCompat* MfcOleValueRuntime_005f7719(OleCurrencyCompat* target,
    const OleCurrencyCompat* source) {
    *target = *source;
    return target;
}

OleCurrencyCompat* MfcOleValueRuntime_005f7745(OleCurrencyCompat* target,
    const VARIANTARG* source) {
    *target = ConstructOleCurrencyFromVariant(*source);
    return target;
}

int MfcOleValueRuntime_005f7761(const OleCurrencyCompat* value) {
    return static_cast<int>(value->status);
}

OleCurrencyCompat* MfcOleValueRuntime_005f78e8(OleCurrencyCompat* value) {
    value->value.int64 = 0;
    value->status = OleDateStatus::Valid;
    return value;
}

OleCurrencyCompat* MfcOleValueRuntime_005f7910(OleCurrencyCompat* target,
    const OleCurrencyCompat* source) {
    return MfcOleValueRuntime_005f7719(target, source);
}

OleDateTimeCompat* MfcOleValueRuntime_005f793c(OleDateTimeCompat* target,
    const VARIANTARG* source) {
    *target = ConstructOleDateTimeFromVariant(*source);
    return target;
}

OleDateTimeCompat* MfcOleValueRuntime_005f7980(OleDateTimeCompat* target,
    std::time_t value) {
    *target = ConstructOleDateTimeFromTimeT(value);
    return target;
}

OleDateTimeCompat* MfcOleValueRuntime_005f799c(OleDateTimeCompat* target,
    const SYSTEMTIME* source) {
    if (!EncodeOleDateTime(source->wYear,
            source->wMonth, source->wDay, source->wHour, source->wMinute,
            source->wSecond, *target)) {
        target->value = 0.0;
        target->status = OleDateStatus::Invalid;
    }
    return target;
}

OleDateTimeCompat* MfcOleValueRuntime_005f79b8(OleDateTimeCompat* target,
    const FILETIME* source) {
    *target = ConstructOleDateTimeFromFileTime(*source);
    return target;
}

OleDateTimeCompat* MfcOleValueRuntime_005f7a33(OleDateTimeCompat* target,
    const OleDateTimeCompat* source) {
    *target = *source;
    return target;
}

int MfcOleValueRuntime_005f7a5f(const OleDateTimeCompat* value) {
    return static_cast<int>(value->status);
}

bool MfcOleValueRuntime_005f7a86(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return lhs.status == rhs.status && lhs.value == rhs.value;
}

bool MfcOleValueRuntime_005f7ac7(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return !MfcOleValueRuntime_005f7a86(lhs, rhs);
}

OleDateTimeCompat* MfcOleValueRuntime_005f7b08(OleDateTimeCompat* target,
    const OleDateTimeCompat* value, const OleDateTimeSpanCompat* span) {
    *target = AddOleDateTimeSpan(*value, *span);
    return target;
}

OleDateTimeCompat* MfcOleValueRuntime_005f7b33(OleDateTimeCompat* target,
    const OleDateTimeCompat* value, const OleDateTimeSpanCompat* span) {
    OleDateTimeSpanCompat negated = *span;
    negated.days = -negated.days;
    *target = AddOleDateTimeSpan(*value, negated);
    return target;
}

double MfcOleValueRuntime_005f7b5e(const OleDateTimeCompat& value) {
    return value.value;
}

OleDateTimeSpanCompat* MfcOleValueRuntime_005f7bbf(
    OleDateTimeSpanCompat* span) {
    span->days = 0.0;
    span->status = OleDateStatus::Valid;
    return span;
}

OleDateTimeSpanCompat* MfcOleValueRuntime_005f7c0f(
    OleDateTimeSpanCompat* target, const OleDateTimeSpanCompat* source) {
    *target = *source;
    return target;
}

int MfcOleValueRuntime_005f7c63(const OleDateTimeSpanCompat* span) {
    return static_cast<int>(span->status);
}

double MfcOleValueRuntime_005f7c8a(const OleDateTimeSpanCompat& span) {
    return ValidOleSpanValueWithAfxOleAssert(span, 0xd4);
}

int MfcOleValueRuntime_005f7cc6(const OleDateTimeSpanCompat& span) {
    return OleSpanTotalUnitsWithAfxOleAssert(span, kOleHoursPerDay, 0xd6);
}

int MfcOleValueRuntime_005f7d1b(const OleDateTimeSpanCompat& span) {
    return OleSpanTotalUnitsWithAfxOleAssert(
        span, kOleHoursPerDay * kOleMinutesPerHour, 0xdb);
}

int MfcOleValueRuntime_005f7d76(const OleDateTimeSpanCompat& span) {
    return OleSpanTotalUnitsWithAfxOleAssert(span,
        kOleHoursPerDay * kOleMinutesPerHour * kOleMinutesPerHour,
        0xe0);
}

int MfcOleValueRuntime_005f7dd7(const OleDateTimeSpanCompat& span) {
    return static_cast<int>(ValidOleSpanValueWithAfxOleAssert(span, 0xe6));
}

bool MfcOleValueRuntime_005f7e18(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    return lhs.status == rhs.status && lhs.days == rhs.days;
}

bool MfcOleValueRuntime_005f7e59(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    return !MfcOleValueRuntime_005f7e18(lhs, rhs);
}

bool MfcOleValueRuntime_005f7e9a(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    return ValidOleSpanValueWithAfxOleAssert(lhs, 0xf1) <
        ValidOleSpanValueWithAfxOleAssert(rhs, 0xf2);
}

bool MfcOleValueRuntime_005f7f1f(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    const double lhs_days = ValidOleSpanValueWithAfxOleAssert(lhs, 0xf6);
    const double rhs_days = ValidOleSpanValueWithAfxOleAssert(rhs, 0xf7);
    return rhs_days < lhs_days;
}

bool MfcOleValueRuntime_005f7fa4(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    return ValidOleSpanValueWithAfxOleAssert(lhs, 0xfb) <=
        ValidOleSpanValueWithAfxOleAssert(rhs, 0xfc);
}

bool MfcOleValueRuntime_005f8029(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    const double lhs_days = ValidOleSpanValueWithAfxOleAssert(lhs, 0x100);
    const double rhs_days = ValidOleSpanValueWithAfxOleAssert(rhs, 0x101);
    return rhs_days <= lhs_days;
}

OleDateTimeSpanCompat* MfcOleValueRuntime_005f80ae(
    OleDateTimeSpanCompat* target, const OleDateTimeSpanCompat* lhs,
    const OleDateTimeSpanCompat* rhs) {
    *target = AddOleDateTimeSpans(*lhs, *rhs);
    return target;
}

OleDateTimeSpanCompat* MfcOleValueRuntime_005f80d9(
    OleDateTimeSpanCompat* target, const OleDateTimeSpanCompat* lhs,
    const OleDateTimeSpanCompat* rhs) {
    *target = SubtractOleDateTimeSpans(*lhs, *rhs);
    return target;
}

double MfcOleValueRuntime_005f8129(const OleDateTimeSpanCompat& span) {
    return span.days;
}

COleSafeArray* MfcOleValueRuntime_005f8139(COleSafeArray* value) {
    if (value != nullptr) {
        InitializeOleVariant(*value);
    }
    return value;
}

void MfcOleValueRuntime_005f8158(COleSafeArray* variant) {
    MfcOleValueRuntime_005f816b(variant);
}

void MfcOleValueRuntime_005f816b(COleSafeArray* variant) {
    ClearVariantWithAfxOleAssert(variant, 0x115);
}

void* MfcOleValueRuntime_005f81a4(void* value) {
    return value;
}

void* MfcOleValueRuntime_005f81b2(void* value) {
    return value;
}

MfcOleExceptionCompat* MfcOleValueRuntime_005f81f0(
    MfcOleExceptionCompat* exception) {
    if (exception != nullptr) {
        ConstructSimpleException(*exception);
        exception->scode = S_OK;
    }
    return exception;
}

MfcOleExceptionCompat* MfcOleValueRuntime_005f8210(
    MfcOleExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    MfcOleValueRuntime_005f73f8(exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(exception);
    }
    return exception;
}

void DumpOleSafeArray(MfcDumpContext& context, const COleSafeArray& safe_array);

namespace {

constexpr VARTYPE kOleVariantTypeMask = 0x0fff;

VARTYPE OleVariantBaseType(const VARIANTARG& variant) {
    return static_cast<VARTYPE>(V_VT(&variant) & kOleVariantTypeMask);
}

void DumpContextWriteInt64(MfcDumpContext& context, long long value) {
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "%lld", value);
    DumpContextWriteString(context, buffer);
}

void DumpOleElementValue(MfcDumpContext& context, VARTYPE type,
    const void* value) {
    switch (type) {
    case VT_I2:
        DumpContextWriteLong(context, *static_cast<const SHORT*>(value));
        break;
    case VT_I4:
    case VT_ERROR:
        DumpContextWriteLong(context, *static_cast<const LONG*>(value));
        break;
    case VT_R4:
        DumpContextWriteFloat(context, *static_cast<const FLOAT*>(value));
        break;
    case VT_R8:
    case VT_DATE:
        DumpContextWriteDouble(context, *static_cast<const DOUBLE*>(value));
        break;
    case VT_CY:
        DumpContextWriteInt64(context,
            static_cast<const CY*>(value)->int64);
        break;
    case VT_BOOL:
        DumpContextWriteString(context,
            *static_cast<const VARIANT_BOOL*>(value) == VARIANT_FALSE
                ? "FALSE"
                : "TRUE");
        break;
    case VT_UI1:
        DumpContextWriteByte(context, *static_cast<const BYTE*>(value));
        break;
    case VT_UI2:
        DumpContextWriteWord(context, *static_cast<const WORD*>(value));
        break;
    case VT_UI4:
        DumpContextWriteULong(context, *static_cast<const ULONG*>(value));
        break;
    default:
        DumpContextWriteString(context, "<unsupported>");
        break;
    }
}

void DumpOleVariantScalar(MfcDumpContext& context,
    const COleVariant& variant) {
    switch (V_VT(&variant)) {
    case VT_EMPTY:
        DumpContextWriteString(context, " empty");
        break;
    case VT_NULL:
        DumpContextWriteString(context, " null");
        break;
    case VT_I2:
        DumpContextWriteString(context, " iVal = ");
        DumpContextWriteLong(context, V_I2(&variant));
        break;
    case VT_I4:
        DumpContextWriteString(context, " lVal = ");
        DumpContextWriteLong(context, V_I4(&variant));
        break;
    case VT_R4:
        DumpContextWriteString(context, " fltVal = ");
        DumpContextWriteFloat(context, V_R4(&variant));
        break;
    case VT_R8:
        DumpContextWriteString(context, " dblVal = ");
        DumpContextWriteDouble(context, V_R8(&variant));
        break;
    case VT_CY:
        DumpContextWriteString(context, " cyVal = ");
        DumpContextWriteInt64(context, V_CY(&variant).int64);
        break;
    case VT_DATE:
        DumpContextWriteString(context, " date = ");
        DumpContextWriteDouble(context, V_DATE(&variant));
        break;
    case VT_BSTR:
        DumpContextWriteString(context, " bstrVal = ");
        DumpContextWriteWideString(context, V_BSTR(&variant));
        break;
    case VT_DISPATCH:
        DumpContextWriteString(context, " pdispVal = ");
        DumpContextWritePointer(context, V_DISPATCH(&variant));
        break;
    case VT_ERROR:
        DumpContextWriteString(context, " scode = ");
        DumpContextWriteLong(context, V_ERROR(&variant));
        break;
    case VT_BOOL:
        DumpContextWriteString(context, " boolVal = ");
        DumpContextWriteString(context,
            V_BOOL(&variant) == VARIANT_FALSE ? "FALSE" : "TRUE");
        break;
    case VT_UNKNOWN:
        DumpContextWriteString(context, " punkVal = ");
        DumpContextWritePointer(context, V_UNKNOWN(&variant));
        break;
    case VT_UI1:
        DumpContextWriteString(context, " bVal = ");
        DumpContextWriteByte(context, V_UI1(&variant));
        break;
    default:
        DumpContextWriteString(context, " value = <unsupported>");
        break;
    }
}

} // namespace

void DumpOleVariant(MfcDumpContext& context, const COleVariant& variant) {
    DumpContextWriteString(context, "COleVariant Object ");
    DumpContextWriteString(context, "vt = ");
    DumpContextWriteWord(context, V_VT(&variant));

    if ((V_VT(&variant) & VT_BYREF) != 0) {
        DumpContextWriteString(context, " byref = ");
        DumpContextWritePointer(context, V_BYREF(&variant));
        DumpContextWriteString(context, "\n");
        return;
    }

    if ((V_VT(&variant) & VT_ARRAY) != 0) {
        DumpContextWriteString(context, "\n");
        DumpOleSafeArray(context, variant);
        return;
    }

    DumpOleVariantScalar(context, variant);
    DumpContextWriteString(context, "\n");
}

void DumpOleSafeArrayElement(MfcDumpContext& context,
    const COleSafeArray& safe_array, LONG* indices) {
    if (indices == nullptr || (V_VT(&safe_array) & VT_ARRAY) == 0 ||
        V_ARRAY(&safe_array) == nullptr) {
        DumpContextWriteString(context, "<invalid>");
        return;
    }

    const VARTYPE element_type = OleVariantBaseType(safe_array);
    if (element_type == VT_VARIANT) {
        COleVariant value{};
        InitializeOleVariant(value);
        const HRESULT hr = SafeArrayGetElement(
            V_ARRAY(const_cast<COleSafeArray*>(&safe_array)), indices, &value);
        if (FAILED(hr)) {
            DumpContextWriteString(context, "<error>");
            return;
        }
        DumpOleVariant(context, value);
        ClearOleVariant(value);
        return;
    }

    if (element_type == VT_BSTR) {
        BSTR value = nullptr;
        const HRESULT hr = SafeArrayGetElement(
            V_ARRAY(const_cast<COleSafeArray*>(&safe_array)), indices, &value);
        if (FAILED(hr)) {
            DumpContextWriteString(context, "<error>");
            return;
        }
        DumpContextWriteWideString(context, value);
        SysFreeString(value);
        return;
    }

    if (element_type == VT_UNKNOWN || element_type == VT_DISPATCH) {
        IUnknown* value = nullptr;
        const HRESULT hr = SafeArrayGetElement(
            V_ARRAY(const_cast<COleSafeArray*>(&safe_array)), indices, &value);
        if (FAILED(hr)) {
            DumpContextWriteString(context, "<error>");
            return;
        }
        DumpContextWritePointer(context, value);
        if (value != nullptr) {
            value->Release();
        }
        return;
    }

    alignas(16) unsigned char storage[sizeof(VARIANTARG)]{};
    const HRESULT hr = SafeArrayGetElement(
        V_ARRAY(const_cast<COleSafeArray*>(&safe_array)), indices, storage);
    if (FAILED(hr)) {
        DumpContextWriteString(context, "<error>");
        return;
    }
    DumpOleElementValue(context, element_type, storage);
}

void DumpOleSafeArray(MfcDumpContext& context,
    const COleSafeArray& safe_array) {
    DumpContextWriteString(context, "COleSafeArray Object ");
    DumpContextWriteString(context, "vt = ");
    DumpContextWriteWord(context, V_VT(&safe_array));

    if ((V_VT(&safe_array) & VT_ARRAY) == 0 ||
        V_ARRAY(&safe_array) == nullptr) {
        DumpContextWriteString(context, " array = NULL\n");
        return;
    }

    SAFEARRAY* array = V_ARRAY(const_cast<COleSafeArray*>(&safe_array));
    const UINT dimensions = SafeArrayGetDim(array);
    DumpContextWriteString(context, " bounds = ");

    std::vector<LONG> lower(dimensions, 0);
    std::vector<LONG> upper(dimensions, -1);
    for (UINT dimension = 1; dimension <= dimensions; ++dimension) {
        LONG low = 0;
        LONG high = -1;
        if (FAILED(SafeArrayGetLBound(array, dimension, &low)) ||
            FAILED(SafeArrayGetUBound(array, dimension, &high))) {
            DumpContextWriteString(context, "<error>");
            DumpContextWriteString(context, "\n");
            return;
        }
        lower[dimension - 1] = low;
        upper[dimension - 1] = high;
        DumpContextWriteString(context, "[");
        DumpContextWriteLong(context, low);
        DumpContextWriteString(context, "..");
        DumpContextWriteLong(context, high);
        DumpContextWriteString(context, "]");
    }
    DumpContextWriteString(context, "\n");

    if (DumpContextGetDepth(context) <= 0 || dimensions == 0) {
        return;
    }

    std::vector<LONG> indices = lower;
    constexpr std::size_t kMaxDumpedSafeArrayElements = 256;
    for (std::size_t dumped = 0; dumped < kMaxDumpedSafeArrayElements;
         ++dumped) {
        DumpContextWriteString(context, "  [");
        for (UINT dimension = 0; dimension < dimensions; ++dimension) {
            if (dimension != 0) {
                DumpContextWriteString(context, ",");
            }
            DumpContextWriteLong(context, indices[dimension]);
        }
        DumpContextWriteString(context, "] = ");
        DumpOleSafeArrayElement(context, safe_array, indices.data());
        DumpContextWriteString(context, "\n");

        int dimension = static_cast<int>(dimensions) - 1;
        while (dimension >= 0) {
            if (indices[dimension] < upper[dimension]) {
                ++indices[dimension];
                for (UINT reset = static_cast<UINT>(dimension + 1);
                     reset < dimensions; ++reset) {
                    indices[reset] = lower[reset];
                }
                break;
            }
            --dimension;
        }
        if (dimension < 0) {
            return;
        }
    }
    DumpContextWriteString(context, "  ...\n");
}

template <typename Type>
void DumpElements(MfcDumpContext& context, const Type* values, int count) {
    (void)context;
    (void)values;
    (void)count;
}

template <>
void DumpElements<COleVariant>(MfcDumpContext& context,
    const COleVariant* values, int count) {
    if (values == nullptr || count <= 0) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        DumpOleVariant(context, values[index]);
    }
}

#ifdef _WIN32
MfcRuntimeClassCompat* GetCWinThreadRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CWinThread", static_cast<int>(sizeof(MfcWinThreadCompat)), 0xffff,
        +[]() -> void* {
            auto* thread = new MfcWinThreadCompat();
            thread->runtime_class = GetCWinThreadRuntimeClass();
            return thread;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

unsigned __stdcall WinThreadStartThunk(void* context) {
    auto* thread = static_cast<MfcWinThreadCompat*>(context);
    if (thread == nullptr) {
        return static_cast<unsigned>(-1);
    }

    g_current_win_thread = thread;
    if (thread->runtime_class == nullptr) {
        thread->runtime_class = GetCWinThreadRuntimeClass();
    }
    AfxWinInitThread(*thread);

    const unsigned exit_code = WinThreadStartEpilogue(*thread);
    if (g_current_win_thread == thread) {
        g_current_win_thread = nullptr;
    }

    const bool auto_delete = thread->auto_delete;
    thread->thread = nullptr;
    if (auto_delete) {
        delete thread;
    }
    return exit_code;
}

unsigned WinThreadStartEpilogue(MfcWinThreadCompat& thread) {
    if (thread.thread_proc != nullptr) {
        return thread.thread_proc(thread.thread_params);
    }
    if (WinThreadInitInstance(thread) == 0) {
        return WinThreadExitInstance(thread);
    }
    return static_cast<unsigned>(WinThreadRun(thread));
}

MfcWinThreadCompat* AfxGetThreadCompat() {
    if (g_current_win_thread != nullptr) {
        return g_current_win_thread;
    }
    return g_app_win_thread;
}

HINSTANCE AfxGetInstanceHandleCompat() {
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr && app->instance != nullptr) {
        return app->instance;
    }
    return GetModuleHandleA(nullptr);
}

HINSTANCE AfxGetResourceHandleCompat() {
    if (g_resource_handle != nullptr) {
        return g_resource_handle;
    }
    return AfxGetInstanceHandleCompat();
}

void AfxSetResourceHandleCompat(HINSTANCE instance) {
    if (instance != nullptr) {
        g_resource_handle = instance;
    }
}

const char* AfxGetAppNameCompat() {
    static std::string fallback_name;
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr && !app->app_name.empty()) {
        return app->app_name.c_str();
    }
    if (fallback_name.empty()) {
        char module_path[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, module_path,
                static_cast<DWORD>(sizeof(module_path))) != 0) {
            const char* base_name = std::strrchr(module_path, '\\');
            const char* slash_name = std::strrchr(module_path, '/');
            if (slash_name != nullptr &&
                (base_name == nullptr || slash_name > base_name)) {
                base_name = slash_name;
            }
            fallback_name = base_name == nullptr ? module_path : base_name + 1;
        } else {
            fallback_name = "Ranker";
        }
    }
    return fallback_name.c_str();
}

MfcOccManagerCompat* AfxGetOccManagerCompat() {
    MfcBaseModuleStateCompat* state = MfcModuleStateRuntime_005e95f3();
    return state == nullptr ? nullptr : state->occ_manager;
}

MfcOccManagerCompat* AfxSetOccManagerCompat(MfcOccManagerCompat* manager) {
    MfcBaseModuleStateCompat* state = MfcModuleStateRuntime_005e95f3();
    if (state == nullptr) {
        return nullptr;
    }
    MfcOccManagerCompat* previous = state->occ_manager;
    state->occ_manager = manager;
    return previous;
}

MfcCWndCompat* WinThreadGetMainWndInline(MfcWinThreadCompat& thread) {
    return thread.main_window == nullptr ? nullptr : CWndFromHandle(thread.main_window);
}

MfcRuntimeClassCompat* GetWinAppRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CWinApp", static_cast<int>(sizeof(MfcWinAppCompat)), 0xffff,
        +[]() -> void* {
            auto* app = new MfcWinAppCompat();
            ConstructWinApp(*app);
            return app;
        },
        GetCWinThreadRuntimeClass(), nullptr};
    return &runtime_class;
}

void DestroyWinThreadCompat(MfcWinThreadCompat& thread) {
    if (g_current_win_thread == &thread) {
        g_current_win_thread = nullptr;
    }
    if (g_app_win_thread == &thread) {
        g_app_win_thread = nullptr;
    }
    DestroyAuxWinThread(thread);
}

MfcWinAppCompat& ConstructWinApp(MfcWinAppCompat& app, const char* app_name) {
    app = MfcWinAppCompat{};
    app.runtime_class = GetWinAppRuntimeClass();
    ConstructCmdTarget(app.command_target);
    app.command_target.owner = &app;
    app.instance = GetModuleHandleA(nullptr);
    if (g_resource_handle == nullptr) {
        g_resource_handle = app.instance;
    }
    app.previous_instance = nullptr;
    app.command_show = SW_SHOWNORMAL;
    const char* command_line = GetCommandLineA();
    app.command_line = command_line == nullptr ? "" : command_line;

    char module_path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, module_path,
            static_cast<DWORD>(sizeof(module_path))) != 0) {
        const char* base_name = std::strrchr(module_path, '\\');
        const char* slash_name = std::strrchr(module_path, '/');
        if (slash_name != nullptr && (base_name == nullptr || slash_name > base_name)) {
            base_name = slash_name;
        }
        app.exe_name = base_name == nullptr ? module_path : base_name + 1;
    }
    if (app_name != nullptr && *app_name != '\0') {
        app.app_name = app_name;
    } else {
        app.app_name = app.exe_name;
    }
    app.profile_name = app.app_name;
    if (!app.exe_name.empty()) {
        app.help_file_path = app.exe_name + ".hlp";
    }
    if (g_app_win_thread == nullptr) {
        g_app_win_thread = &app;
    }
    return app;
}

void DestroyWinApp(MfcWinAppCompat& app) {
    if (app.printer_dev_mode != nullptr) {
        GlobalFree(app.printer_dev_mode);
        app.printer_dev_mode = nullptr;
    }
    if (app.printer_dev_names != nullptr) {
        GlobalFree(app.printer_dev_names);
        app.printer_dev_names = nullptr;
    }
    if (g_app_win_thread == &app) {
        g_app_win_thread = nullptr;
    }
    if (g_current_win_thread == &app) {
        g_current_win_thread = nullptr;
    }
    DestroyCmdTarget(app.command_target);
    DestroyWinThreadCompat(app);
}

int WinAppRun(MfcWinAppCompat& app) {
    if (app.main_window == nullptr) {
        AfxTraceOutput("Warning: m_pMainWnd is NULL in CWinApp::Run - "
            "quitting application.\n");
        PostQuitMessage(0);
    }
    return WinThreadRun(app);
}

void WinAppSendIdleUpdate(MfcWinAppCompat& app, WPARAM wparam, LPARAM lparam) {
    HWND main_window = app.main_window;
    if (main_window == nullptr) {
        MfcWinThreadCompat* thread = AfxGetThreadCompat();
        if (thread != nullptr) {
            main_window = thread->main_window;
        }
    }
    if (main_window == nullptr || IsWindow(main_window) == FALSE) {
        return;
    }

    MfcCWndCompat* main = CWndFromHandle(main_window);
    if (main != nullptr) {
        CWndAssertValid(*main);
    }
    app.help_mode = false;
    send_message_to_descendants(main_window, kMfcIdleUpdateCmdUiMessage,
        wparam, lparam);
    SendMessageA(main_window, kMfcKickIdleMessage, 0, 0);
}

bool WinAppOnIdle(MfcWinAppCompat& app, long count) {
    if (count < 1) {
        (void)WinThreadOnIdle(app, count);
        WinAppSendIdleUpdate(app, TRUE, 0);
        return true;
    }
    if (count == 1 && WinThreadOnIdle(app, count)) {
        AfxTraceOutput("Warning: CWinThread::OnIdle returned TRUE for "
            "CWinApp idle count 1.\n");
    }
    return false;
}

void WinAppUpdatePrinterSelection(MfcWinAppCompat& app,
    const char* printer_name) {
    if (app.printer_dev_names == nullptr || printer_name == nullptr) {
        return;
    }
    auto* dev_names = static_cast<DEVNAMES*>(GlobalLock(app.printer_dev_names));
    if (dev_names == nullptr) {
        return;
    }

    const char* stored_printer =
        reinterpret_cast<const char*>(dev_names) + dev_names->wDeviceOffset;
    const bool matches = stored_printer != nullptr &&
        lstrcmpiA(stored_printer, printer_name) == 0;
    GlobalUnlock(app.printer_dev_names);

    if (matches) {
        AfxTraceOutput("Printer selection '%s' already matches stored "
            "DEVNAMES; keeping current DEVMODE.\n", printer_name);
    }
}

void WinAppAssertValid(MfcWinAppCompat& app) {
    AfxAssertValidObject(&app, "appcore.cpp", 0x220);
    if (app.runtime_class == nullptr) {
        app.runtime_class = GetWinAppRuntimeClass();
    }
    if (app.instance == nullptr) {
        app.instance = GetModuleHandleA(nullptr);
    }
    if (AfxGetThreadCompat() == &app && app.doc_manager != nullptr) {
        AfxAssertValidObject(static_cast<const MfcObjectCompat*>(app.doc_manager),
            "appcore.cpp", 0x227);
    }
}

void WinAppDump(const MfcWinAppCompat& app) {
    WinThreadDump(app);
    AfxTraceOutput("m_hInstance = %p\n", app.instance);
    AfxTraceOutput("m_hPrevInstance = %p\n", app.previous_instance);
    AfxTraceOutput("m_lpCmdLine = %s\n", app.command_line.c_str());
    AfxTraceOutput("m_nCmdShow = %d\n", app.command_show);
    AfxTraceOutput("m_pszAppName = %s\n", app.app_name.c_str());
    AfxTraceOutput("m_bHelpMode = %d\n", app.help_mode ? 1 : 0);
    AfxTraceOutput("m_pszExeName = %s\n", app.exe_name.c_str());
    AfxTraceOutput("m_pszHelpFilePath = %s\n", app.help_file_path.c_str());
    AfxTraceOutput("m_pszProfileName = %s\n", app.profile_name.c_str());
    AfxTraceOutput("m_hDevMode = %p\n", app.printer_dev_mode);
    AfxTraceOutput("m_hDevNames = %p\n", app.printer_dev_names);
    AfxTraceOutput("m_dwPromptContext = %p\n",
        reinterpret_cast<void*>(app.prompt_context));
    AfxTraceOutput("m_pDocManager = %p\n", app.doc_manager);
    AfxTraceOutput("m_nWaitCursorCount = %d\n", g_wait_cursor_count);
    AfxTraceOutput("m_hcurWaitCursorRestore = %p\n", g_previous_wait_cursor);
    AfxTraceOutput("m_msgCur.hwnd = %p\n", app.current_message.hwnd);
    AfxTraceOutput("m_msgCur.message = 0x%04x\n",
        app.current_message.message);
    AfxTraceOutput("m_msgCur.wParam = %p\n",
        reinterpret_cast<void*>(app.current_message.wParam));
    AfxTraceOutput("m_msgCur.lParam = %p\n",
        reinterpret_cast<void*>(app.current_message.lParam));
    AfxTraceOutput("m_msgCur.pt = (%ld,%ld)\n", app.current_message.pt.x,
        app.current_message.pt.y);
}

MfcWinAppCompat* AfxGetAppCompat() {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr &&
        ObjectIsKindOfRuntimeClass(thread, GetWinAppRuntimeClass())) {
        return static_cast<MfcWinAppCompat*>(thread);
    }
    if (g_app_win_thread != nullptr &&
        ObjectIsKindOfRuntimeClass(g_app_win_thread, GetWinAppRuntimeClass())) {
        return static_cast<MfcWinAppCompat*>(g_app_win_thread);
    }
    return nullptr;
}

void AfxOleOnReleaseAllObjects() {
    if (AfxOleGetUserCtrl()) {
        return;
    }
    AfxOleSetUserCtrl(true);

    MfcWinAppCompat* app = AfxGetAppCompat();
    HWND main_window = app == nullptr ? nullptr : app->main_window;
    if (main_window != nullptr && IsWindow(main_window)) {
        if (IsWindowEnabled(main_window)) {
            if (MfcCWndCompat* main = CWndFromHandlePermanent(main_window)) {
                CWndDestroyWindow(*main);
            } else {
                DestroyWindow(main_window);
            }
        }
        return;
    }

    MfcWinThreadCompat& thread = ole_thread_state();
    if (!thread.ole_post_quit_disabled) {
        PostQuitMessage(0);
    }
}

bool AfxOleCanExitApp() {
    return ole_thread_state().ole_object_count == 0;
}

void AfxOleLockApp() {
    InterlockedIncrement(&ole_thread_state().ole_object_count);
}

void AfxOleUnlockApp() {
    MfcWinThreadCompat& thread = ole_thread_state();
    if (thread.ole_object_count == 0) {
        AfxTraceOutput("olelock.cpp(0x45): AfxOleUnlockApp without a lock.\n");
        return;
    }
    const LONG remaining = InterlockedDecrement(&thread.ole_object_count);
    if (remaining == 0) {
        AfxOleOnReleaseAllObjects();
    }
}

void AfxOleSetUserCtrl(bool user_control) {
    MfcWinThreadCompat& thread = ole_thread_state();
    if (user_control && !thread.ole_user_control) {
        MfcWinAppCompat* app = AfxGetAppCompat();
        HWND main_window = app == nullptr ? nullptr : app->main_window;
        if (main_window == nullptr || !IsWindow(main_window) ||
            !IsWindowVisible(main_window)) {
            AfxTraceOutput("Warning: AfxOleSetUserCtrl(TRUE) without a "
                "visible main window.\n");
        }
    }
    thread.ole_user_control = user_control;
}

bool AfxOleGetUserCtrl() {
    return ole_thread_state().ole_user_control;
}

const char* AfxGetScodeString(SCODE scode) {
    switch (scode) {
    case S_OK:
        return "S_OK";
    case S_FALSE:
        return "S_FALSE";
    case E_UNEXPECTED:
        return "E_UNEXPECTED";
    case E_NOTIMPL:
        return "E_NOTIMPL";
    case E_OUTOFMEMORY:
        return "E_OUTOFMEMORY";
    case E_INVALIDARG:
        return "E_INVALIDARG";
    case E_NOINTERFACE:
        return "E_NOINTERFACE";
    case E_POINTER:
        return "E_POINTER";
    case E_HANDLE:
        return "E_HANDLE";
    case E_ABORT:
        return "E_ABORT";
    case E_FAIL:
        return "E_FAIL";
    case E_ACCESSDENIED:
        return "E_ACCESSDENIED";
    default:
        return nullptr;
    }
}

const char* AfxGetScodeRangeString(SCODE scode) {
    struct Range {
        SCODE first;
        SCODE last;
        const char* name;
    };
    static constexpr Range ranges[] = {
#ifdef CACHE_E_FIRST
        {static_cast<SCODE>(CACHE_E_FIRST), static_cast<SCODE>(CACHE_E_LAST),
            "CACHE_E_FIRST..CACHE_E_LAST"},
#endif
#ifdef CLASSFACTORY_E_FIRST
        {static_cast<SCODE>(CLASSFACTORY_E_FIRST),
            static_cast<SCODE>(CLASSFACTORY_E_LAST),
            "CLASSFACTORY_E_FIRST..CLASSFACTORY_E_LAST"},
#endif
#ifdef CLIPBRD_E_FIRST
        {static_cast<SCODE>(CLIPBRD_E_FIRST),
            static_cast<SCODE>(CLIPBRD_E_LAST),
            "CLIPBRD_E_FIRST..CLIPBRD_E_LAST"},
#endif
#ifdef CO_E_FIRST
        {static_cast<SCODE>(CO_E_FIRST), static_cast<SCODE>(CO_E_LAST),
            "CO_E_FIRST..CO_E_LAST"},
#endif
#ifdef CONVERT10_E_FIRST
        {static_cast<SCODE>(CONVERT10_E_FIRST),
            static_cast<SCODE>(CONVERT10_E_LAST),
            "CONVERT10_E_FIRST..CONVERT10_E_LAST"},
#endif
#ifdef DATA_E_FIRST
        {static_cast<SCODE>(DATA_E_FIRST), static_cast<SCODE>(DATA_E_LAST),
            "DATA_E_FIRST..DATA_E_LAST"},
#endif
#ifdef DISP_E_FIRST
        {static_cast<SCODE>(DISP_E_FIRST), static_cast<SCODE>(DISP_E_LAST),
            "DISP_E_FIRST..DISP_E_LAST"},
#endif
#ifdef DRAGDROP_E_FIRST
        {static_cast<SCODE>(DRAGDROP_E_FIRST),
            static_cast<SCODE>(DRAGDROP_E_LAST),
            "DRAGDROP_E_FIRST..DRAGDROP_E_LAST"},
#endif
#ifdef DV_E_FIRST
        {static_cast<SCODE>(DV_E_FIRST), static_cast<SCODE>(DV_E_LAST),
            "DV_E_FIRST..DV_E_LAST"},
#endif
#ifdef INPLACE_E_FIRST
        {static_cast<SCODE>(INPLACE_E_FIRST),
            static_cast<SCODE>(INPLACE_E_LAST),
            "INPLACE_E_FIRST..INPLACE_E_LAST"},
#endif
#ifdef MK_E_FIRST
        {static_cast<SCODE>(MK_E_FIRST), static_cast<SCODE>(MK_E_LAST),
            "MK_E_FIRST..MK_E_LAST"},
#endif
#ifdef OLE_E_FIRST
        {static_cast<SCODE>(OLE_E_FIRST), static_cast<SCODE>(OLE_E_LAST),
            "OLE_E_FIRST..OLE_E_LAST"},
#endif
#ifdef REGDB_E_FIRST
        {static_cast<SCODE>(REGDB_E_FIRST), static_cast<SCODE>(REGDB_E_LAST),
            "REGDB_E_FIRST..REGDB_E_LAST"},
#endif
#ifdef VIEW_E_FIRST
        {static_cast<SCODE>(VIEW_E_FIRST), static_cast<SCODE>(VIEW_E_LAST),
            "VIEW_E_FIRST..VIEW_E_LAST"},
#endif
    };
    for (const Range& range : ranges) {
        if (scode >= range.first && scode <= range.last) {
            return range.name;
        }
    }
    return nullptr;
}

const char* AfxGetFacilityString(SCODE scode) {
    static constexpr const char* facilities[] = {
        "FACILITY_NULL",
        "FACILITY_RPC",
        "FACILITY_DISPATCH",
        "FACILITY_STORAGE",
        "FACILITY_ITF",
        "FACILITY_WIN32",
        "FACILITY_WINDOWS",
        "FACILITY_SSPI",
        "FACILITY_CONTROL",
    };
    const unsigned facility = (static_cast<unsigned long>(scode) >> 16) & 0x1fffU;
    if (facility < (sizeof(facilities) / sizeof(facilities[0]))) {
        return facilities[facility];
    }
    return "<Unknown Facility>";
}

DVTARGETDEVICE* _AfxOleCopyTargetDevice(const DVTARGETDEVICE* source) {
    if (source == nullptr || source->tdSize == 0) {
        return nullptr;
    }
    auto* copy = static_cast<DVTARGETDEVICE*>(CoTaskMemAlloc(source->tdSize));
    if (copy == nullptr) {
        return nullptr;
    }
    std::memcpy(copy, source, source->tdSize);
    return copy;
}

namespace {

std::string FormatOleGuidCompat(REFGUID guid) {
    char text[64]{};
    std::snprintf(text, sizeof(text),
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

std::wstring AnsiToWideStringCompat(LPCSTR text) {
    if (text == nullptr || *text == '\0') {
        return {};
    }
    const int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), count);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

std::string WideToAnsiStringCompat(LPCWSTR text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int count = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0,
        nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string ansi(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_ACP, 0, text, -1, ansi.data(), count, nullptr,
        nullptr);
    if (!ansi.empty() && ansi.back() == '\0') {
        ansi.pop_back();
    }
    return ansi;
}

DWORD WideStringByteCountCompat(LPCWSTR text) {
    return text == nullptr ? 0
        : static_cast<DWORD>((lstrlenW(text) + 1) * sizeof(wchar_t));
}

bool CopyWideStringIntoTargetDevice(BYTE* base, WORD& offset, DWORD& cursor,
    LPCWSTR text) {
    const DWORD bytes = WideStringByteCountCompat(text);
    if (bytes == 0) {
        offset = 0;
        return true;
    }
    if (cursor > std::numeric_limits<WORD>::max()) {
        return false;
    }
    offset = static_cast<WORD>(cursor);
    std::memcpy(base + cursor, text, bytes);
    cursor += bytes;
    return true;
}

DVTARGETDEVICE* CreateTargetDeviceFromWideStrings(LPCWSTR driver,
    LPCWSTR device, LPCWSTR port, const DEVMODEW* devmode) {
    const DWORD header = static_cast<DWORD>(offsetof(DVTARGETDEVICE, tdData));
    const DWORD driver_bytes = WideStringByteCountCompat(driver);
    const DWORD device_bytes = WideStringByteCountCompat(device);
    const DWORD port_bytes = WideStringByteCountCompat(port);
    const DWORD devmode_bytes = devmode == nullptr ? 0
        : static_cast<DWORD>(devmode->dmSize + devmode->dmDriverExtra);
    const DWORD total = header + driver_bytes + device_bytes + port_bytes +
        devmode_bytes;
    if (total == header || total > std::numeric_limits<WORD>::max()) {
        return nullptr;
    }

    auto* target = static_cast<DVTARGETDEVICE*>(CoTaskMemAlloc(total));
    if (target == nullptr) {
        return nullptr;
    }
    std::memset(target, 0, total);
    target->tdSize = total;

    auto* base = reinterpret_cast<BYTE*>(target);
    DWORD cursor = header;
    if (!CopyWideStringIntoTargetDevice(base, target->tdDriverNameOffset,
            cursor, driver) ||
        !CopyWideStringIntoTargetDevice(base, target->tdDeviceNameOffset,
            cursor, device) ||
        !CopyWideStringIntoTargetDevice(base, target->tdPortNameOffset,
            cursor, port)) {
        CoTaskMemFree(target);
        return nullptr;
    }
    if (devmode != nullptr && devmode_bytes != 0) {
        target->tdExtDevmodeOffset = static_cast<WORD>(cursor);
        std::memcpy(base + cursor, devmode, devmode_bytes);
    }
    return target;
}

const DEVMODEW* TargetDeviceDevMode(const DVTARGETDEVICE* target) {
    if (target == nullptr || target->tdExtDevmodeOffset == 0 ||
        target->tdExtDevmodeOffset >= target->tdSize) {
        return nullptr;
    }
    return reinterpret_cast<const DEVMODEW*>(
        reinterpret_cast<const BYTE*>(target) + target->tdExtDevmodeOffset);
}

LPCWSTR TargetDeviceStringAt(const DVTARGETDEVICE* target, WORD offset) {
    if (target == nullptr || offset == 0 || offset >= target->tdSize) {
        return nullptr;
    }
    return reinterpret_cast<LPCWSTR>(
        reinterpret_cast<const BYTE*>(target) + offset);
}

std::vector<BYTE> ConvertDevModeAToWideBytes(const DEVMODEA* source) {
    if (source == nullptr) {
        return {};
    }
    const UINT bytes = sizeof(DEVMODEW) + source->dmDriverExtra;
    std::vector<BYTE> storage(bytes);
    auto* wide = reinterpret_cast<DEVMODEW*>(storage.data());
    ConvertDevModeAToW(wide, source);
    wide->dmSize = sizeof(DEVMODEW);
    wide->dmDriverExtra = source->dmDriverExtra;
    return storage;
}

std::vector<BYTE> ConvertDevModeWToAnsiBytes(const DEVMODEW* source) {
    if (source == nullptr) {
        return {};
    }
    const UINT bytes = sizeof(DEVMODEA) + source->dmDriverExtra;
    std::vector<BYTE> storage(bytes);
    auto* ansi = reinterpret_cast<DEVMODEA*>(storage.data());
    ConvertDevModeWToA(ansi, source);
    ansi->dmSize = sizeof(DEVMODEA);
    ansi->dmDriverExtra = source->dmDriverExtra;
    return storage;
}

const char* OleKnownInterfaceName(REFGUID iid) {
    struct KnownInterface {
        const GUID* iid;
        const char* name;
    };
    static const KnownInterface known[] = {
        {&IID_IUnknown, "IID_IUnknown"},
        {&IID_IClassFactory, "IID_IClassFactory"},
        {&IID_IMarshal, "IID_IMarshal"},
        {&IID_IMalloc, "IID_IMalloc"},
        {&IID_IDataObject, "IID_IDataObject"},
        {&IID_IEnumFORMATETC, "IID_IEnumFORMATETC"},
        {&IID_IStream, "IID_IStream"},
        {&IID_IStorage, "IID_IStorage"},
        {&IID_IOleObject, "IID_IOleObject"},
        {&IID_IOleLink, "IID_IOleLink"},
        {&IID_IViewObject, "IID_IViewObject"},
        {&IID_IViewObject2, "IID_IViewObject2"},
        {&IID_IDropSource, "IID_IDropSource"},
        {&IID_IDropTarget, "IID_IDropTarget"},
    };
    for (const KnownInterface& entry : known) {
        if (IsEqualGUID(iid, *entry.iid)) {
            return entry.name;
        }
    }
    return nullptr;
}

} // namespace

const char* MfcOleRuntime_005f4aba(SCODE scode) {
    return FAILED(scode) ? "SEVERITY_ERROR" : "SEVERITY_SUCCESS";
}

const char* MfcOleRuntime_005f4b02(SCODE scode) {
    static thread_local char buffer[192];
    if (const char* exact = AfxGetScodeString(scode)) {
        std::snprintf(buffer, sizeof(buffer), "%s ($%08lX)", exact,
            static_cast<unsigned long>(scode));
    } else if (const char* range = AfxGetScodeRangeString(scode)) {
        std::snprintf(buffer, sizeof(buffer), "range: %s ($%08lX)", range,
            static_cast<unsigned long>(scode));
    } else {
        std::snprintf(buffer, sizeof(buffer), "severity: %s, facility: %s ($%08lX)",
            MfcOleRuntime_005f4aba(scode), AfxGetFacilityString(scode),
            static_cast<unsigned long>(scode));
    }
    return buffer;
}

[[noreturn]] void MfcOleRuntime_005f4b9a(SCODE scode) {
    const char* text = MfcOleRuntime_005f4b02(scode);
    AfxTraceOutput("Warning: constructing COleException, scode = %s.\n", text);
    throw std::runtime_error(text);
}

BOOL MfcOleRuntime_005f4c41(const MfcOleExceptionCompat& exception,
    LPSTR buffer, int max_chars, DWORD* help_context) {
    if (help_context != nullptr) {
        *help_context = 0;
    }
    if (buffer == nullptr || max_chars <= 0) {
        return FALSE;
    }

    LPSTR message = nullptr;
    const DWORD chars = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(exception.scode),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message), 0, nullptr);
    if (chars == 0 || message == nullptr) {
        buffer[0] = '\0';
        return FALSE;
    }
    lstrcpynA(buffer, message, max_chars);
    LocalFree(message);
    return TRUE;
}

HMENU MfcOleRuntime_005f4d4a(HMENU shared_menu, HMENU source_menu,
    UINT* widths, int group, BOOL append_to_file_menu) {
    if (shared_menu == nullptr || source_menu == nullptr ||
        !IsMenu(shared_menu) || !IsMenu(source_menu)) {
        return nullptr;
    }

    HMENU merged_submenu = nullptr;
    UINT insert_at = widths != nullptr && group == 1 ? widths[0]
        : static_cast<UINT>(GetMenuItemCount(shared_menu));
    UINT copied_in_group = 0;
    const int item_count = GetMenuItemCount(source_menu);
    for (int index = 0; index < item_count; ++index) {
        HMENU submenu = GetSubMenu(source_menu, index);
        UINT state = GetMenuState(source_menu, index, MF_BYPOSITION);
        if (submenu == nullptr && (state & MF_SEPARATOR) != 0U) {
            if (widths != nullptr && group >= 0 && group < 6) {
                widths[group] = copied_in_group;
            }
            copied_in_group = 0;
            group += 2;
            if (widths != nullptr && group >= 0 && group < 6) {
                insert_at += widths[group];
            }
            continue;
        }

        char text[256]{};
        GetMenuStringA(source_menu, index, text, sizeof(text), MF_BYPOSITION);
        if (submenu == nullptr) {
            const UINT item_id = GetMenuItemID(source_menu, index);
            InsertMenuA(shared_menu, insert_at, state | MF_BYPOSITION, item_id,
                text);
            ++insert_at;
            ++copied_in_group;
            continue;
        }

        if (append_to_file_menu != FALSE && group == 5 && widths != nullptr &&
            widths[5] == 1) {
            HMENU file_menu = GetSubMenu(shared_menu, insert_at);
            if (file_menu != nullptr) {
                std::string caption = AfxGetAppNameCompat();
                if (!caption.empty()) {
                    caption.push_back(' ');
                }
                caption += text;
                AppendMenuA(file_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
                    caption.c_str());
                widths[5] = 0;
                if (group > 0) {
                    ++widths[group - 1];
                }
                merged_submenu = submenu;
                continue;
            }
        }

        if (GetMenuItemCount(submenu) > 0) {
            InsertMenuA(shared_menu, insert_at, state | MF_BYPOSITION | MF_POPUP,
                reinterpret_cast<UINT_PTR>(submenu), text);
            ++insert_at;
            ++copied_in_group;
        }
    }
    if (widths != nullptr && group >= 0 && group < 6) {
        widths[group] = copied_in_group;
    }
    return merged_submenu;
}

void MfcOleRuntime_005f50f6(HMENU shared_menu, HMENU source_menu,
    HMENU submenu_to_remove) {
    if (shared_menu == nullptr || source_menu == nullptr ||
        !IsMenu(shared_menu) || !IsMenu(source_menu)) {
        return;
    }

    for (int shared_index = GetMenuItemCount(shared_menu) - 1;
         shared_index >= 0; --shared_index) {
        HMENU shared_submenu = GetSubMenu(shared_menu, shared_index);
        if (shared_submenu == nullptr) {
            continue;
        }
        if (submenu_to_remove != nullptr) {
            const int child_count = GetMenuItemCount(shared_submenu);
            for (int child = 0; child < child_count; ++child) {
                if (GetSubMenu(shared_submenu, child) == submenu_to_remove) {
                    RemoveMenu(shared_submenu, child, MF_BYPOSITION);
                    return;
                }
            }
            continue;
        }
        const int source_count = GetMenuItemCount(source_menu);
        for (int source_index = 0; source_index < source_count; ++source_index) {
            if (GetSubMenu(source_menu, source_index) == shared_submenu) {
                RemoveMenu(shared_menu, shared_index, MF_BYPOSITION);
                break;
            }
        }
    }
}

FORMATETC* MfcOleRuntime_005f528e(FORMATETC* format, CLIPFORMAT clip_format,
    FORMATETC* storage) {
    if (format == nullptr && clip_format != 0 && storage != nullptr) {
        storage->cfFormat = clip_format;
        storage->ptd = nullptr;
        storage->dwAspect = DVASPECT_CONTENT;
        storage->lindex = -1;
        storage->tymed = TYMED_HGLOBAL | TYMED_FILE | TYMED_ISTREAM |
            TYMED_ISTORAGE | TYMED_GDI | TYMED_MFPICT | TYMED_ENHMF;
        return storage;
    }
    return format;
}

HGLOBAL MfcOleRuntime_005f5309(HGLOBAL destination, HGLOBAL source) {
    if (source == nullptr) {
        return nullptr;
    }
    const SIZE_T bytes = GlobalSize(source);
    if (bytes == 0) {
        return nullptr;
    }
    if (destination == nullptr) {
        destination = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (destination == nullptr) {
            return nullptr;
        }
    } else if (GlobalSize(destination) < bytes) {
        return nullptr;
    }

    void* source_ptr = GlobalLock(source);
    void* destination_ptr = GlobalLock(destination);
    if (source_ptr == nullptr || destination_ptr == nullptr) {
        if (source_ptr != nullptr) {
            GlobalUnlock(source);
        }
        if (destination_ptr != nullptr) {
            GlobalUnlock(destination);
        }
        return nullptr;
    }
    std::memcpy(destination_ptr, source_ptr, bytes);
    GlobalUnlock(destination);
    GlobalUnlock(source);
    return destination;
}

BOOL MfcOleRuntime_005f540a(CLIPFORMAT clip_format, STGMEDIUM* destination,
    const STGMEDIUM* source) {
    if (destination == nullptr || source == nullptr ||
        source->tymed == TYMED_NULL) {
        return FALSE;
    }

    STGMEDIUM copy{};
    copy.tymed = source->tymed;
    copy.pUnkForRelease = nullptr;
    switch (source->tymed) {
    case TYMED_HGLOBAL:
        copy.hGlobal = MfcOleRuntime_005f5309(nullptr, source->hGlobal);
        break;
    case TYMED_FILE:
        if (source->lpszFileName != nullptr) {
            const SIZE_T bytes =
                (lstrlenW(source->lpszFileName) + 1) * sizeof(wchar_t);
            copy.lpszFileName = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
            if (copy.lpszFileName != nullptr) {
                std::memcpy(copy.lpszFileName, source->lpszFileName, bytes);
            }
        }
        break;
    case TYMED_ISTREAM:
        copy.pstm = source->pstm;
        if (copy.pstm != nullptr) {
            copy.pstm->AddRef();
        }
        break;
    case TYMED_ISTORAGE:
        copy.pstg = source->pstg;
        if (copy.pstg != nullptr) {
            copy.pstg->AddRef();
        }
        break;
    case TYMED_GDI:
        copy.hBitmap = static_cast<HBITMAP>(
            OleDuplicateData(source->hBitmap, clip_format, 0));
        break;
    case TYMED_MFPICT:
        copy.hMetaFilePict = static_cast<HMETAFILEPICT>(
            OleDuplicateData(source->hMetaFilePict, clip_format, 0));
        break;
    case TYMED_ENHMF:
        copy.hEnhMetaFile = static_cast<HENHMETAFILE>(
            OleDuplicateData(source->hEnhMetaFile, clip_format, 0));
        break;
    default:
        return FALSE;
    }

    const bool copied = (copy.tymed == TYMED_ISTREAM && copy.pstm != nullptr) ||
        (copy.tymed == TYMED_ISTORAGE && copy.pstg != nullptr) ||
        (copy.tymed == TYMED_FILE && copy.lpszFileName != nullptr) ||
        (copy.tymed == TYMED_HGLOBAL && copy.hGlobal != nullptr) ||
        (copy.tymed == TYMED_GDI && copy.hBitmap != nullptr) ||
        (copy.tymed == TYMED_MFPICT && copy.hMetaFilePict != nullptr) ||
        (copy.tymed == TYMED_ENHMF && copy.hEnhMetaFile != nullptr);
    if (!copied) {
        return FALSE;
    }
    *destination = copy;
    return TRUE;
}

HGLOBAL MfcOleRuntime_005f5b1e(DWORD clsid_data1, DWORD clsid_data2,
    DWORD clsid_data3, DWORD clsid_data4, DWORD draw_aspect, LONG width,
    LONG height, LONG point_x, LONG point_y, DWORD status, LPCWSTR user_type,
    LPCWSTR source_display_name) {
    const DWORD user_chars =
        user_type == nullptr ? 0 : static_cast<DWORD>(lstrlenW(user_type) + 1);
    if (source_display_name == nullptr || *source_display_name == L'\0') {
        source_display_name = user_type;
    }
    const DWORD source_chars = source_display_name == nullptr ? 0
        : static_cast<DWORD>(lstrlenW(source_display_name) + 1);
    const DWORD bytes = 0x34 + (user_chars + source_chars) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE,
        bytes);
    if (memory == nullptr) {
        return nullptr;
    }

    auto* fields = static_cast<DWORD*>(GlobalLock(memory));
    if (fields == nullptr) {
        GlobalFree(memory);
        return nullptr;
    }
    fields[0] = bytes;
    fields[1] = clsid_data1;
    fields[2] = clsid_data2;
    fields[3] = clsid_data3;
    fields[4] = clsid_data4;
    fields[5] = draw_aspect;
    fields[6] = static_cast<DWORD>(width);
    fields[7] = static_cast<DWORD>(height);
    fields[8] = static_cast<DWORD>(point_x);
    fields[9] = static_cast<DWORD>(point_y);
    fields[10] = status;

    auto* base = reinterpret_cast<BYTE*>(fields);
    DWORD cursor = 0x34;
    if (user_type != nullptr) {
        fields[11] = cursor;
        const DWORD user_bytes = user_chars * sizeof(wchar_t);
        std::memcpy(base + cursor, user_type, user_bytes);
        cursor += user_bytes;
    }
    if (source_display_name != nullptr) {
        fields[12] = cursor;
        const DWORD source_bytes = source_chars * sizeof(wchar_t);
        std::memcpy(base + cursor, source_display_name, source_bytes);
    }
    GlobalUnlock(memory);
    return memory;
}

HGLOBAL MfcOleRuntime_005f5c99(IOleObject* object, LPCOLESTR source_display_name,
    DWORD draw_aspect, LONG point_x, LONG point_y, const SIZEL* size) {
    if (object == nullptr) {
        return nullptr;
    }

    CLSID clsid{};
    if (FAILED(object->GetUserClassID(&clsid))) {
        clsid = CLSID_NULL;
    }
    LPOLESTR user_type = nullptr;
    object->GetUserType(USERCLASSTYPE_FULL, &user_type);

    SIZEL extent{};
    if (size != nullptr) {
        extent = *size;
    } else {
        object->GetExtent(draw_aspect, &extent);
    }
    DWORD status = 0;
    object->GetMiscStatus(draw_aspect, &status);

    const HGLOBAL descriptor = MfcOleRuntime_005f5b1e(clsid.Data1,
        (static_cast<DWORD>(clsid.Data2) << 16) | clsid.Data3,
        (static_cast<DWORD>(clsid.Data4[0]) << 24) |
            (static_cast<DWORD>(clsid.Data4[1]) << 16) |
            (static_cast<DWORD>(clsid.Data4[2]) << 8) | clsid.Data4[3],
        (static_cast<DWORD>(clsid.Data4[4]) << 24) |
            (static_cast<DWORD>(clsid.Data4[5]) << 16) |
            (static_cast<DWORD>(clsid.Data4[6]) << 8) | clsid.Data4[7],
        draw_aspect, extent.cx, extent.cy, point_x, point_y, status, user_type,
        source_display_name);
    CoTaskMemFree(user_type);
    return descriptor;
}

HRESULT MfcOleRuntime_005f60b0(LPSTORAGE storage, REFCLSID target_class) {
    if (storage == nullptr) {
        return E_POINTER;
    }

    CLSID original_class{};
    HRESULT hr = ReadClassStg(storage, &original_class);
    if (FAILED(hr)) {
        return hr;
    }

    CLIPFORMAT original_format = 0;
    LPOLESTR original_user_type = nullptr;
    ReadFmtUserTypeStg(storage, &original_format, &original_user_type);

    OLECHAR empty[] = L"";
    LPOLESTR new_user_type = nullptr;
    if (FAILED(OleRegGetUserType(target_class, USERCLASSTYPE_FULL,
            &new_user_type))) {
        new_user_type = empty;
    }

    hr = WriteClassStg(storage, target_class);
    if (SUCCEEDED(hr)) {
        hr = WriteFmtUserTypeStg(storage, original_format, new_user_type);
    }
    if (SUCCEEDED(hr)) {
        hr = SetConvertStg(storage, TRUE);
    }
    if (FAILED(hr)) {
        WriteClassStg(storage, original_class);
        WriteFmtUserTypeStg(storage, original_format, original_user_type);
    }
    if (new_user_type != empty) {
        CoTaskMemFree(new_user_type);
    }
    CoTaskMemFree(original_user_type);
    return hr;
}

HRESULT _AfxOleDoTreatAsClass(const char* user_type, REFCLSID old_class,
    REFCLSID new_class) {
    HRESULT hr = CoTreatAsClass(old_class, new_class);
    if (SUCCEEDED(hr) || user_type == nullptr) {
        return hr;
    }

    HKEY clsid_key = nullptr;
    if (RegOpenKeyA(HKEY_CLASSES_ROOT, "CLSID", &clsid_key) != ERROR_SUCCESS) {
        return hr;
    }
    LPOLESTR old_class_text = nullptr;
    if (SUCCEEDED(StringFromCLSID(old_class, &old_class_text))) {
        const std::string ansi_class = WideToAnsiStringCompat(old_class_text);
        if (!ansi_class.empty()) {
            RegSetValueA(clsid_key, ansi_class.c_str(), REG_SZ, user_type,
                lstrlenA(user_type));
            hr = CoTreatAsClass(old_class, new_class);
        }
        CoTaskMemFree(old_class_text);
    }
    RegCloseKey(clsid_key);
    return hr;
}

DVTARGETDEVICE* MfcOleRuntime_005f62c4(const DEVNAMES* devnames,
    const DEVMODEA* devmode) {
    if (devnames == nullptr) {
        return nullptr;
    }
    const char* base = reinterpret_cast<const char*>(devnames);
    const char* driver = devnames->wDriverOffset == 0 ? nullptr
        : base + devnames->wDriverOffset;
    const char* device = devnames->wDeviceOffset == 0 ? nullptr
        : base + devnames->wDeviceOffset;
    const char* port = devnames->wOutputOffset == 0 ? nullptr
        : base + devnames->wOutputOffset;

    const std::wstring wide_driver = AnsiToWideStringCompat(driver);
    const std::wstring wide_device = AnsiToWideStringCompat(device);
    const std::wstring wide_port = AnsiToWideStringCompat(port);
    const std::vector<BYTE> wide_devmode = ConvertDevModeAToWideBytes(devmode);
    const auto* wide_devmode_ptr = wide_devmode.empty() ? nullptr
        : reinterpret_cast<const DEVMODEW*>(wide_devmode.data());
    return CreateTargetDeviceFromWideStrings(
        wide_driver.empty() ? nullptr : wide_driver.c_str(),
        wide_device.empty() ? nullptr : wide_device.c_str(),
        wide_port.empty() ? nullptr : wide_port.c_str(), wide_devmode_ptr);
}

DVTARGETDEVICE* AfxOleCreateTargetDevice(HGLOBAL devnames_handle,
    HGLOBAL devmode_handle) {
    if (devnames_handle == nullptr) {
        return nullptr;
    }
    const auto* devnames =
        static_cast<const DEVNAMES*>(GlobalLock(devnames_handle));
    if (devnames == nullptr) {
        return nullptr;
    }
    const auto* devmode = devmode_handle == nullptr ? nullptr
        : static_cast<const DEVMODEA*>(GlobalLock(devmode_handle));
    DVTARGETDEVICE* target = MfcOleRuntime_005f62c4(devnames, devmode);
    if (devmode != nullptr) {
        GlobalUnlock(devmode_handle);
    }
    GlobalUnlock(devnames_handle);
    return target;
}

IUnknown* MfcOleRuntime_005f66bb(IOleObject* object) {
    if (object == nullptr) {
        return nullptr;
    }

    IOleLink* link = nullptr;
    if (SUCCEEDED(object->QueryInterface(IID_IOleLink,
            reinterpret_cast<void**>(&link))) &&
        link != nullptr) {
        IUnknown* source = nullptr;
        if (FAILED(link->GetBoundSource(&source))) {
            source = nullptr;
        }
        link->Release();
        if (source != nullptr) {
            return source;
        }
    }

    object->AddRef();
    return object;
}

int MfcOleRuntime_005f6793(IOleObject* object) {
    IUnknown* source = MfcOleRuntime_005f66bb(object);
    if (source == nullptr) {
        return 0;
    }

    IOleLink* link = nullptr;
    int length = 0;
    if (SUCCEEDED(source->QueryInterface(IID_IOleLink,
            reinterpret_cast<void**>(&link))) &&
        link != nullptr) {
        LPOLESTR display_name = nullptr;
        if (SUCCEEDED(link->GetSourceDisplayName(&display_name)) &&
            display_name != nullptr) {
            length = lstrlenW(display_name);
            CoTaskMemFree(display_name);
        }
        link->Release();
    }
    source->Release();
    return length;
}

void MfcOleRuntime_005f6944(FORMATETC& destination,
    const FORMATETC& source) {
    destination = source;
    destination.ptd = _AfxOleCopyTargetDevice(source.ptd);
}

HDC MfcOleRuntime_005f69d3(const DVTARGETDEVICE* target) {
    if (target == nullptr) {
        return CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
    }

    const std::string driver = WideToAnsiStringCompat(
        TargetDeviceStringAt(target, target->tdDriverNameOffset));
    const std::string device = WideToAnsiStringCompat(
        TargetDeviceStringAt(target, target->tdDeviceNameOffset));
    const std::string port = WideToAnsiStringCompat(
        TargetDeviceStringAt(target, target->tdPortNameOffset));
    const std::vector<BYTE> devmode =
        ConvertDevModeWToAnsiBytes(TargetDeviceDevMode(target));
    auto* devmode_ptr = devmode.empty() ? nullptr
        : reinterpret_cast<const DEVMODEA*>(devmode.data());
    return CreateDCA(driver.empty() ? nullptr : driver.c_str(),
        device.empty() ? nullptr : device.c_str(),
        port.empty() ? nullptr : port.c_str(), devmode_ptr);
}

void _AfxDeleteMetafilePict(void* metafile_pict) {
    if (metafile_pict == nullptr) {
        return;
    }
    STGMEDIUM medium{};
    medium.tymed = TYMED_MFPICT;
    medium.hMetaFilePict = static_cast<HMETAFILEPICT>(metafile_pict);
    ReleaseStgMedium(&medium);
}

void _AfxXformSizeInPixelsToHimetric(HDC dc, const SIZE* pixels,
    SIZE* himetric) {
    if (pixels == nullptr || himetric == nullptr) {
        return;
    }
    int dpi_x = dc == nullptr ? 0 : GetDeviceCaps(dc, LOGPIXELSX);
    int dpi_y = dc == nullptr ? 0 : GetDeviceCaps(dc, LOGPIXELSY);
    if (dpi_x <= 0) {
        dpi_x = 96;
    }
    if (dpi_y <= 0) {
        dpi_y = 96;
    }
    himetric->cx = MulDiv(2540, pixels->cx, dpi_x);
    himetric->cy = MulDiv(2540, pixels->cy, dpi_y);
}

void _AfxXformSizeInHimetricToPixels(HDC dc, const SIZE* himetric,
    SIZE* pixels) {
    if (pixels == nullptr || himetric == nullptr) {
        return;
    }
    int dpi_x = dc == nullptr ? 0 : GetDeviceCaps(dc, LOGPIXELSX);
    int dpi_y = dc == nullptr ? 0 : GetDeviceCaps(dc, LOGPIXELSY);
    if (dpi_x <= 0) {
        dpi_x = 96;
    }
    if (dpi_y <= 0) {
        dpi_y = 96;
    }
    pixels->cx = MulDiv(dpi_x, himetric->cx, 2540);
    pixels->cy = MulDiv(dpi_y, himetric->cy, 2540);
}

const char* MfcOleRuntime_005f6cf0(REFGUID iid) {
    if (const char* name = OleKnownInterfaceName(iid)) {
        return name;
    }
    static thread_local std::string text;
    text = FormatOleGuidCompat(iid);
    return text.c_str();
}

void* MfcOleRuntime_005f6db6(IUnknown* object, REFIID iid) {
    if (object == nullptr) {
        return nullptr;
    }
    void* queried = nullptr;
    if (FAILED(object->QueryInterface(iid, &queried))) {
        return nullptr;
    }
    return queried;
}

ULONG MfcOleRuntime_005f6e0b(IUnknown** object) {
    if (object == nullptr || *object == nullptr) {
        return 0;
    }
    IUnknown* released = *object;
    *object = nullptr;
    return released->Release();
}

MfcOleInnerUnknownCompat* MfcOleRuntime_005f6e66(
    MfcOleInnerUnknownCompat* inner, IUnknown* controlling_unknown) {
    if (inner != nullptr) {
        inner->controlling_unknown = controlling_unknown;
        inner->reference_count = 1;
    }
    return inner;
}

ULONG MfcOleRuntime_005f6e97(MfcOleInnerUnknownCompat& inner) {
    if (inner.controlling_unknown != nullptr) {
        return inner.controlling_unknown->AddRef();
    }
    return MfcOleValueRuntime_005f7414(inner);
}

ULONG MfcOleRuntime_005f6ec7(MfcOleInnerUnknownCompat& inner) {
    return ReleaseOleInnerUnknownBody(inner);
}

ULONG MfcOleRuntime_005f6f34(MfcOleInnerUnknownCompat& inner) {
    if (inner.controlling_unknown != nullptr) {
        return inner.controlling_unknown->Release();
    }
    return MfcOleRuntime_005f6ec7(inner);
}

void* MfcOleRuntime_005f6f64(MfcOleInnerUnknownCompat& inner, REFIID iid) {
    if (inner.controlling_unknown != nullptr) {
        return MfcOleRuntime_005f6db6(inner.controlling_unknown, iid);
    }
    if (IsEqualGUID(iid, IID_IUnknown)) {
        MfcOleValueRuntime_005f7414(inner);
        return OleInnerUnknownThis(inner);
    }
    return nullptr;
}

void* MfcOleRuntime_005f7131(MfcOleInnerUnknownCompat& inner, REFIID iid) {
    return MfcOleRuntime_005f6f64(inner, iid);
}

namespace {

VARIANTARG BuildDispatchVariantFromArg(unsigned char encoded_type,
    va_list& args) {
    VARIANTARG variant{};
    InitializeOleVariant(variant);

    bool by_ref = (encoded_type & 0x40U) != 0U;
    VARTYPE type = static_cast<VARTYPE>(encoded_type & ~0x40U);
    if (by_ref) {
        V_VT(&variant) = static_cast<VARTYPE>(type | VT_BYREF);
        switch (type) {
        case VT_I2:
            V_I2REF(&variant) = va_arg(args, SHORT*);
            break;
        case VT_I4:
        case VT_ERROR:
            V_I4REF(&variant) = va_arg(args, LONG*);
            break;
        case VT_R4:
            V_R4REF(&variant) = va_arg(args, FLOAT*);
            break;
        case VT_R8:
        case VT_DATE:
            V_R8REF(&variant) = va_arg(args, DOUBLE*);
            break;
        case VT_CY:
            V_CYREF(&variant) = va_arg(args, CY*);
            break;
        case VT_BSTR:
            V_BSTRREF(&variant) = va_arg(args, BSTR*);
            break;
        case VT_DISPATCH:
            V_DISPATCHREF(&variant) = va_arg(args, IDispatch**);
            break;
        case VT_BOOL: {
            auto* value = va_arg(args, VARIANT_BOOL*);
            if (value != nullptr) {
                *value = *value == 0 ? VARIANT_FALSE : VARIANT_TRUE;
            }
            V_BOOLREF(&variant) = value;
            break;
        }
        case VT_VARIANT:
            V_VARIANTREF(&variant) = va_arg(args, VARIANTARG*);
            break;
        case VT_UNKNOWN:
            V_UNKNOWNREF(&variant) = va_arg(args, IUnknown**);
            break;
        case VT_UI1:
            V_UI1REF(&variant) = va_arg(args, BYTE*);
            break;
        default:
            V_BYREF(&variant) = va_arg(args, void*);
            break;
        }
        return variant;
    }

    switch (type) {
    case VT_I2:
        V_VT(&variant) = VT_I2;
        V_I2(&variant) = static_cast<SHORT>(va_arg(args, int));
        break;
    case VT_I4:
        V_VT(&variant) = VT_I4;
        V_I4(&variant) = va_arg(args, LONG);
        break;
    case VT_R4:
        V_VT(&variant) = VT_R4;
        V_R4(&variant) = static_cast<FLOAT>(va_arg(args, double));
        break;
    case VT_R8:
        V_VT(&variant) = VT_R8;
        V_R8(&variant) = va_arg(args, DOUBLE);
        break;
    case VT_CY:
        V_VT(&variant) = VT_CY;
        V_CY(&variant) = *va_arg(args, CY*);
        break;
    case VT_DATE:
        V_VT(&variant) = VT_DATE;
        V_DATE(&variant) = va_arg(args, DATE);
        break;
    case VT_BSTR: {
        V_VT(&variant) = VT_BSTR;
        LPCOLESTR text = va_arg(args, LPCOLESTR);
        V_BSTR(&variant) = SysAllocString(text);
        break;
    }
    case VT_DISPATCH:
        V_VT(&variant) = VT_DISPATCH;
        V_DISPATCH(&variant) = va_arg(args, IDispatch*);
        break;
    case VT_ERROR:
        V_VT(&variant) = VT_ERROR;
        V_ERROR(&variant) = va_arg(args, SCODE);
        break;
    case VT_BOOL:
        V_VT(&variant) = VT_BOOL;
        V_BOOL(&variant) = va_arg(args, int) == 0 ? VARIANT_FALSE : VARIANT_TRUE;
        break;
    case VT_VARIANT:
        variant = *va_arg(args, VARIANTARG*);
        break;
    case VT_UNKNOWN:
        V_VT(&variant) = VT_UNKNOWN;
        V_UNKNOWN(&variant) = va_arg(args, IUnknown*);
        break;
    case 0x0e: {
        V_VT(&variant) = VT_BSTR;
        LPCSTR text = va_arg(args, LPCSTR);
        const std::wstring wide = AnsiToWideStringCompat(text);
        V_BSTR(&variant) = SysAllocString(wide.empty() ? nullptr : wide.c_str());
        break;
    }
    case VT_UI1:
        V_VT(&variant) = VT_UI1;
        V_UI1(&variant) = static_cast<BYTE>(va_arg(args, int));
        break;
    default:
        V_VT(&variant) = VT_EMPTY;
        break;
    }
    return variant;
}

void CopyDispatchResult(VARIANTARG& result, VARTYPE return_type,
    void* return_value) {
    if (return_value == nullptr || return_type == VT_EMPTY) {
        return;
    }
    if (return_type != VT_VARIANT && V_VT(&result) != return_type) {
        HRESULT hr = ChangeOleVariantType(result, return_type, &result);
        if (FAILED(hr)) {
            MfcOleRuntime_005f4b9a(hr);
        }
    }

    switch (return_type) {
    case VT_I2:
        *static_cast<SHORT*>(return_value) = V_I2(&result);
        break;
    case VT_I4:
    case VT_ERROR:
        *static_cast<LONG*>(return_value) = V_I4(&result);
        break;
    case VT_R4:
        *static_cast<FLOAT*>(return_value) = V_R4(&result);
        break;
    case VT_R8:
    case VT_DATE:
        *static_cast<DOUBLE*>(return_value) = V_R8(&result);
        break;
    case VT_CY:
        *static_cast<CY*>(return_value) = V_CY(&result);
        break;
    case VT_BSTR:
        *static_cast<BSTR*>(return_value) = SysAllocString(V_BSTR(&result));
        break;
    case VT_DISPATCH:
        *static_cast<IDispatch**>(return_value) = V_DISPATCH(&result);
        if (V_DISPATCH(&result) != nullptr) {
            V_DISPATCH(&result)->AddRef();
        }
        break;
    case VT_BOOL:
        *static_cast<BOOL*>(return_value) = V_BOOL(&result) != VARIANT_FALSE;
        break;
    case VT_VARIANT:
        CopyOleVariant(*static_cast<VARIANTARG*>(return_value), result);
        break;
    case VT_UNKNOWN:
        *static_cast<IUnknown**>(return_value) = V_UNKNOWN(&result);
        if (V_UNKNOWN(&result) != nullptr) {
            V_UNKNOWN(&result)->AddRef();
        }
        break;
    case VT_UI1:
        *static_cast<BYTE*>(return_value) = V_UI1(&result);
        break;
    default:
        break;
    }
}

} // namespace

HRESULT MfcOleDispatchRuntime_005fa050(LPCSTR prog_id_or_clsid,
    LPCLSID clsid) {
    if (prog_id_or_clsid == nullptr || clsid == nullptr) {
        return E_POINTER;
    }
    const std::wstring wide = AnsiToWideStringCompat(prog_id_or_clsid);
    if (wide.empty()) {
        return E_INVALIDARG;
    }
    if (prog_id_or_clsid[0] == '{') {
        return CLSIDFromString(wide.c_str(), clsid);
    }
    return CLSIDFromProgID(wide.c_str(), clsid);
}

MfcOleDispatchDriverCompat* MfcOleDispatchRuntime_005fa164(
    MfcOleDispatchDriverCompat* driver, IDispatch* dispatch,
    bool auto_release) {
    driver->dispatch = dispatch;
    driver->auto_release = auto_release;
    return driver;
}

MfcOleDispatchDriverCompat* MfcOleDispatchRuntime_005fa185(
    MfcOleDispatchDriverCompat* driver,
    const MfcOleDispatchDriverCompat* source) {
    if (driver == nullptr || source == nullptr || driver == source) {
        return driver;
    }
    driver->dispatch = source->dispatch;
    if (driver->dispatch != nullptr) {
        driver->dispatch->AddRef();
    }
    driver->auto_release = true;
    return driver;
}

MfcOleDispatchDriverCompat* MfcOleDispatchRuntime_005fa1e6(
    MfcOleDispatchDriverCompat* driver,
    const MfcOleDispatchDriverCompat* source) {
    if (driver == nullptr || source == nullptr || driver == source) {
        return driver;
    }
    IDispatch* old_dispatch = driver->dispatch;
    const bool release_old = driver->auto_release;
    driver->dispatch = source->dispatch;
    if (driver->dispatch != nullptr) {
        driver->dispatch->AddRef();
    }
    driver->auto_release = true;
    if (old_dispatch != nullptr && release_old) {
        old_dispatch->Release();
    }
    return driver;
}

BOOL MfcOleDispatchRuntime_005fa24f(MfcOleDispatchDriverCompat& driver,
    REFCLSID clsid, MfcOleExceptionCompat* exception) {
    if (driver.dispatch != nullptr) {
        return FALSE;
    }

    driver.auto_release = true;
    IUnknown* unknown = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_SERVER, IID_IUnknown,
        reinterpret_cast<void**>(&unknown));
    if (hr == REGDB_E_CLASSNOTREG) {
        hr = CoCreateInstance(clsid, nullptr,
            CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IUnknown,
            reinterpret_cast<void**>(&unknown));
    }
    if (SUCCEEDED(hr) && unknown != nullptr) {
        hr = OleRun(unknown);
    }
    if (SUCCEEDED(hr) && unknown != nullptr) {
        driver.dispatch = static_cast<IDispatch*>(
            MfcOleRuntime_005f6db6(unknown, IID_IDispatch));
    }
    if (unknown != nullptr) {
        unknown->Release();
    }
    if (driver.dispatch != nullptr) {
        return TRUE;
    }
    if (exception != nullptr) {
        exception->scode = hr;
    }
    AfxTraceOutput("Warning: CreateDispatch returned scode = %s.\n",
        MfcOleRuntime_005f4b02(hr));
    return FALSE;
}

BOOL MfcOleDispatchRuntime_005fa392(MfcOleDispatchDriverCompat& driver,
    LPCSTR prog_id_or_clsid, MfcOleExceptionCompat* exception) {
    if (driver.dispatch != nullptr) {
        return FALSE;
    }
    CLSID clsid{};
    HRESULT hr = MfcOleDispatchRuntime_005fa050(prog_id_or_clsid, &clsid);
    if (FAILED(hr)) {
        if (exception != nullptr) {
            exception->scode = hr;
        }
        return FALSE;
    }
    return MfcOleDispatchRuntime_005fa24f(driver, clsid, exception);
}

void MfcOleDispatchRuntime_005fa3ff(MfcOleDispatchDriverCompat& driver,
    IDispatch* dispatch, bool auto_release) {
    if (dispatch == nullptr) {
        return;
    }
    MfcOleDispatchRuntime_005fa44b(driver);
    driver.dispatch = dispatch;
    driver.auto_release = auto_release;
}

void MfcOleDispatchRuntime_005fa44b(MfcOleDispatchDriverCompat& driver) {
    if (driver.dispatch != nullptr && driver.auto_release) {
        driver.dispatch->Release();
    }
    driver.dispatch = nullptr;
}

IDispatch* MfcOleDispatchRuntime_005fa480(
    MfcOleDispatchDriverCompat& driver) {
    IDispatch* dispatch = driver.dispatch;
    driver.dispatch = nullptr;
    return dispatch;
}

HRESULT MfcOleDispatchRuntime_005fa4a1(MfcOleDispatchDriverCompat& driver,
    DISPID dispatch_id, WORD flags, VARTYPE return_type, void* return_value,
    const unsigned char* param_info, va_list args) {
    if (driver.dispatch == nullptr) {
        AfxTraceOutput("Warning: attempt to call Invoke with null IDispatch.\n");
        return E_POINTER;
    }

    const std::size_t param_count = param_info == nullptr ? 0 : lstrlenA(
        reinterpret_cast<LPCSTR>(param_info));
    std::vector<VARIANTARG> arguments(param_count);
    for (std::size_t index = 0; index < param_count; ++index) {
        arguments[param_count - index - 1] =
            BuildDispatchVariantFromArg(param_info[index], args);
    }

    DISPID named_arg = DISPID_PROPERTYPUT;
    DISPPARAMS params{};
    params.rgvarg = arguments.empty() ? nullptr : arguments.data();
    params.cArgs = static_cast<UINT>(arguments.size());
    if ((flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0) {
        params.rgdispidNamedArgs = &named_arg;
        params.cNamedArgs = 1;
    }

    VARIANTARG result{};
    InitializeOleVariant(result);
    VARIANTARG* result_ptr = return_type == VT_EMPTY ? nullptr : &result;
    EXCEPINFO exception_info{};
    UINT arg_error = 0;
    HRESULT hr = driver.dispatch->Invoke(dispatch_id, IID_NULL,
        LOCALE_USER_DEFAULT, flags, &params, result_ptr, &exception_info,
        &arg_error);

    for (VARIANTARG& argument : arguments) {
        if (V_VT(&argument) == VT_BSTR) {
            ClearOleVariant(argument);
        }
    }

    if (FAILED(hr)) {
        if (hr != DISP_E_EXCEPTION) {
            MfcOleRuntime_005f4b9a(hr);
        }
        SysFreeString(exception_info.bstrSource);
        SysFreeString(exception_info.bstrDescription);
        SysFreeString(exception_info.bstrHelpFile);
        return hr;
    }

    CopyDispatchResult(result, return_type, return_value);
    ClearOleVariant(result);
    return S_OK;
}

namespace {

struct MfcOleDispatchMapEntryCompat {
    const char* name = nullptr;
    DISPID dispatch_id = DISPID_UNKNOWN;
    VARTYPE value_type = VT_EMPTY;
    const unsigned char* param_info = nullptr;
    std::size_t offset = 0;
    HRESULT (*invoke)(MfcCommandTargetCompat&, WORD, DISPPARAMS*, VARIANTARG*,
        EXCEPINFO*, UINT*) = nullptr;
    void (*on_changed)(MfcCommandTargetCompat&) = nullptr;
};

struct MfcOleDispatchMapCompat {
    const MfcOleDispatchMapCompat* base = nullptr;
    const MfcOleDispatchMapEntryCompat* entries = nullptr;
    mutable int cached_count = -1;
};

struct MfcOleDispatchExceptionCompat : MfcSimpleExceptionCompat {
    WORD code = 0;
    std::string description;
    std::string source;
    std::string help_file;
    DWORD help_context = 0;
    SCODE scode = DISP_E_EXCEPTION;
};

bool IsDispatchEntrySentinel(const MfcOleDispatchMapEntryCompat& entry) {
    return entry.name == nullptr && entry.dispatch_id == DISPID_UNKNOWN &&
        entry.value_type == VT_EMPTY && entry.param_info == nullptr &&
        entry.invoke == nullptr;
}

std::size_t DispatchValueSize(VARTYPE type) {
    switch (type & ~VT_BYREF) {
    case VT_I2:
        return sizeof(SHORT);
    case VT_I4:
    case VT_R4:
    case VT_ERROR:
        return sizeof(LONG);
    case VT_R8:
    case VT_DATE:
        return sizeof(DOUBLE);
    case VT_CY:
        return sizeof(CY);
    case VT_BOOL:
        return sizeof(BOOL);
    case VT_BSTR:
    case VT_DISPATCH:
    case VT_UNKNOWN:
        return sizeof(void*);
    case VT_VARIANT:
        return sizeof(VARIANTARG);
    case VT_UI1:
        return sizeof(BYTE);
    default:
        return sizeof(void*);
    }
}

const MfcOleDispatchMapCompat* DispatchMapFromTarget(
    const MfcCommandTargetCompat& target) {
    return static_cast<const MfcOleDispatchMapCompat*>(target.dispatch_map);
}

MfcOleDispatchMapCompat* DispatchMapFromTarget(MfcCommandTargetCompat& target) {
    return static_cast<MfcOleDispatchMapCompat*>(target.dispatch_map);
}

} // namespace

HRESULT MfcOleDispatchRuntimeTail_005fb047(MfcOleDispatchDriverCompat& driver,
    DISPID dispatch_id, VARTYPE value_type, ...) {
    unsigned char param_info[2]{
        static_cast<unsigned char>(value_type == VT_BSTR ? 0x0e : value_type),
        0};
    va_list args;
    va_start(args, value_type);
    HRESULT hr = MfcOleDispatchRuntime_005fa4a1(driver, dispatch_id,
        DISPATCH_PROPERTYPUT, VT_EMPTY, nullptr, param_info, args);
    va_end(args);
    return hr;
}

HRESULT MfcOleDispatchRuntimeTail_005fb0e0(const void* object,
    const MfcOleDispatchMapEntryCompat& entry, VARIANTARG& value) {
    if (object == nullptr) {
        return E_POINTER;
    }
    InitializeOleVariant(value);
    const auto* bytes = static_cast<const BYTE*>(object) + entry.offset;
    switch (entry.value_type) {
    case VT_I2:
        V_VT(&value) = VT_I2;
        V_I2(&value) = *reinterpret_cast<const SHORT*>(bytes);
        break;
    case VT_I4:
        V_VT(&value) = VT_I4;
        V_I4(&value) = *reinterpret_cast<const LONG*>(bytes);
        break;
    case VT_R4:
        V_VT(&value) = VT_R4;
        V_R4(&value) = *reinterpret_cast<const FLOAT*>(bytes);
        break;
    case VT_R8:
        V_VT(&value) = VT_R8;
        V_R8(&value) = *reinterpret_cast<const DOUBLE*>(bytes);
        break;
    case VT_CY:
        V_VT(&value) = VT_CY;
        V_CY(&value) = *reinterpret_cast<const CY*>(bytes);
        break;
    case VT_DATE:
        V_VT(&value) = VT_DATE;
        V_DATE(&value) = *reinterpret_cast<const DATE*>(bytes);
        break;
    case VT_BSTR: {
        V_VT(&value) = VT_BSTR;
        const auto* text = reinterpret_cast<const std::string*>(bytes);
        const std::wstring wide = AnsiToWideStringCompat(text->c_str());
        V_BSTR(&value) = SysAllocString(wide.c_str());
        break;
    }
    case VT_DISPATCH:
        V_VT(&value) = VT_DISPATCH;
        V_DISPATCH(&value) = *reinterpret_cast<IDispatch* const*>(bytes);
        if (V_DISPATCH(&value) != nullptr) {
            V_DISPATCH(&value)->AddRef();
        }
        break;
    case VT_BOOL:
        V_VT(&value) = VT_BOOL;
        V_BOOL(&value) = *reinterpret_cast<const BOOL*>(bytes)
            ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    case VT_VARIANT:
        return CopyOleVariant(value, *reinterpret_cast<const VARIANTARG*>(bytes));
    case VT_UNKNOWN:
        V_VT(&value) = VT_UNKNOWN;
        V_UNKNOWN(&value) = *reinterpret_cast<IUnknown* const*>(bytes);
        if (V_UNKNOWN(&value) != nullptr) {
            V_UNKNOWN(&value)->AddRef();
        }
        break;
    case VT_UI1:
        V_VT(&value) = VT_UI1;
        V_UI1(&value) = *reinterpret_cast<const BYTE*>(bytes);
        break;
    default:
        return DISP_E_MEMBERNOTFOUND;
    }
    return S_OK;
}

HRESULT MfcOleDispatchRuntimeTail_005fb2c4(void* object,
    const MfcOleDispatchMapEntryCompat& entry, const VARIANTARG& source) {
    if (object == nullptr) {
        return E_POINTER;
    }
    VARIANTARG converted{};
    InitializeOleVariant(converted);
    HRESULT hr = entry.value_type == VT_VARIANT ? CopyOleVariant(converted, source)
        : ChangeOleVariantType(converted, entry.value_type, &source);
    if (FAILED(hr)) {
        return hr;
    }

    auto* bytes = static_cast<BYTE*>(object) + entry.offset;
    switch (entry.value_type) {
    case VT_I2:
        *reinterpret_cast<SHORT*>(bytes) = V_I2(&converted);
        break;
    case VT_I4:
        *reinterpret_cast<LONG*>(bytes) = V_I4(&converted);
        break;
    case VT_R4:
        *reinterpret_cast<FLOAT*>(bytes) = V_R4(&converted);
        break;
    case VT_R8:
    case VT_DATE:
        *reinterpret_cast<DOUBLE*>(bytes) = V_R8(&converted);
        break;
    case VT_CY:
        *reinterpret_cast<CY*>(bytes) = V_CY(&converted);
        break;
    case VT_BSTR: {
        auto* text = reinterpret_cast<std::string*>(bytes);
        *text = WideToAnsiStringCompat(V_BSTR(&converted));
        break;
    }
    case VT_DISPATCH:
        *reinterpret_cast<IDispatch**>(bytes) = V_DISPATCH(&converted);
        if (V_DISPATCH(&converted) != nullptr) {
            V_DISPATCH(&converted)->AddRef();
        }
        break;
    case VT_BOOL:
        *reinterpret_cast<BOOL*>(bytes) = V_BOOL(&converted) != VARIANT_FALSE;
        break;
    case VT_VARIANT:
        hr = CopyOleVariant(*reinterpret_cast<VARIANTARG*>(bytes), converted);
        break;
    case VT_UNKNOWN:
        *reinterpret_cast<IUnknown**>(bytes) = V_UNKNOWN(&converted);
        if (V_UNKNOWN(&converted) != nullptr) {
            V_UNKNOWN(&converted)->AddRef();
        }
        break;
    case VT_UI1:
        *reinterpret_cast<BYTE*>(bytes) = V_UI1(&converted);
        break;
    default:
        hr = DISP_E_MEMBERNOTFOUND;
        break;
    }
    ClearOleVariant(converted);
    return hr;
}

UINT MfcOleDispatchRuntimeTail_005fb576(
    const MfcOleDispatchMapCompat* dispatch_map) {
    if (dispatch_map == nullptr || dispatch_map->entries == nullptr) {
        return 0;
    }
    if (dispatch_map->cached_count >= 0) {
        return static_cast<UINT>(dispatch_map->cached_count);
    }
    UINT count = 0;
    while (!IsDispatchEntrySentinel(dispatch_map->entries[count])) {
        ++count;
    }
    dispatch_map->cached_count = static_cast<int>(count);
    return count;
}

DISPID MfcOleDispatchRuntimeTail_005fb612(
    const MfcOleDispatchMapCompat* dispatch_map, LPCSTR name) {
    if (name == nullptr) {
        return DISPID_UNKNOWN;
    }
    for (const MfcOleDispatchMapCompat* map = dispatch_map; map != nullptr;
         map = map->base) {
        const UINT count = MfcOleDispatchRuntimeTail_005fb576(map);
        for (UINT index = 0; index < count; ++index) {
            const MfcOleDispatchMapEntryCompat& entry = map->entries[index];
            if (entry.name != nullptr && lstrcmpiA(entry.name, name) == 0) {
                return entry.dispatch_id != DISPID_UNKNOWN
                    ? entry.dispatch_id
                    : static_cast<DISPID>(index + 1);
            }
        }
    }
    return DISPID_UNKNOWN;
}

const MfcOleDispatchMapEntryCompat* MfcOleDispatchRuntimeTail_005fb717(
    const MfcCommandTargetCompat& target, DISPID dispatch_id) {
    for (const MfcOleDispatchMapCompat* map = DispatchMapFromTarget(target);
         map != nullptr; map = map->base) {
        const UINT count = MfcOleDispatchRuntimeTail_005fb576(map);
        for (UINT index = 0; index < count; ++index) {
            const MfcOleDispatchMapEntryCompat& entry = map->entries[index];
            if (entry.dispatch_id == dispatch_id ||
                (entry.dispatch_id == DISPID_UNKNOWN &&
                    dispatch_id == static_cast<DISPID>(index + 1))) {
                return &entry;
            }
        }
    }
    return nullptr;
}

[[noreturn]] void MfcOleDispatchRuntimeTail_005fdc66(WORD code,
    UINT string_id, int help_id);

void MfcOleDispatchRuntimeTail_005fb890() {
    MfcOleDispatchRuntimeTail_005fdc66(0xf18c, 0xf18c, -1);
}

void MfcOleDispatchRuntimeTail_005fb8ac() {
    MfcOleDispatchRuntimeTail_005fdc66(0xf18d, 0xf18d, -1);
}

void MfcOleDispatchRuntimeTail_005fb8c8(MfcCommandTargetCompat& target,
    MfcOleDispatchMapCompat* dispatch_map) {
    target.dispatch_map = dispatch_map;
}

void* MfcOleDispatchRuntimeTail_005fb95f(MfcCommandTargetCompat& target,
    REFIID iid) {
    if (target.dispatch_map == nullptr) {
        return nullptr;
    }
    MfcOleInnerUnknownCompat inner{};
    return MfcOleRuntime_005f6f64(inner, iid);
}

MfcCommandTargetCompat* MfcOleDispatchRuntimeTail_005fb9c3(void* dispatch_impl) {
    return static_cast<MfcCommandTargetCompat*>(dispatch_impl);
}

BSTR MfcOleDispatchRuntimeTail_005fba6b(const char* text) {
    const std::wstring wide = AnsiToWideStringCompat(text);
    return SysAllocString(wide.empty() ? L"" : wide.c_str());
}

BSTR MfcOleDispatchRuntimeTail_005fbadc(const char* text, BSTR* target) {
    if (target == nullptr) {
        return nullptr;
    }
    const std::wstring wide = AnsiToWideStringCompat(text);
    if (SysReAllocString(target, wide.empty() ? L"" : wide.c_str()) == 0) {
        ThrowMfcMemoryException();
    }
    return *target;
}

int MfcOleDispatchRuntimeTail_005fbba5(const unsigned char* param_info,
    VARTYPE return_type) {
    int bytes = static_cast<int>(DispatchValueSize(return_type));
    if (param_info == nullptr) {
        return bytes;
    }
    for (const unsigned char* cursor = param_info; *cursor != 0; ++cursor) {
        if (*cursor == 0xff) {
            continue;
        }
        const bool by_ref = (*cursor & 0x40U) != 0U;
        const VARTYPE type = static_cast<VARTYPE>(*cursor & ~0x40U);
        bytes += static_cast<int>(by_ref ? sizeof(void*) : DispatchValueSize(type));
    }
    return bytes;
}

HRESULT MfcOleDispatchRuntimeTail_005fbc9e(std::vector<VARIANTARG>& output,
    const unsigned char* param_info, DISPPARAMS& params, UINT* arg_error) {
    output.clear();
    if (param_info == nullptr) {
        return S_OK;
    }
    UINT input_index = 0;
    for (const unsigned char* cursor = param_info; *cursor != 0; ++cursor) {
        if (*cursor == 0xff) {
            continue;
        }
        if (input_index >= params.cArgs) {
            if (arg_error != nullptr) {
                *arg_error = input_index;
            }
            return DISP_E_BADPARAMCOUNT;
        }
        VARIANTARG converted{};
        InitializeOleVariant(converted);
        const VARTYPE type = static_cast<VARTYPE>(*cursor & ~0x40U);
        HRESULT hr = (*cursor & 0x40U) != 0U
            ? CopyOleVariant(converted, params.rgvarg[input_index])
            : ChangeOleVariantType(converted, type, &params.rgvarg[input_index]);
        if (FAILED(hr)) {
            if (arg_error != nullptr) {
                *arg_error = input_index;
            }
            return hr;
        }
        output.push_back(converted);
        ++input_index;
    }
    return S_OK;
}

HRESULT MfcOleDispatchRuntimeTail_005fc3cc(MfcCommandTargetCompat& target,
    const MfcOleDispatchMapEntryCompat& entry, WORD flags, VARIANTARG* result,
    DISPPARAMS* params, EXCEPINFO* exception_info, UINT* arg_error) {
    if (entry.invoke != nullptr) {
        return entry.invoke(target, flags, params, result, exception_info,
            arg_error);
    }
    if ((flags & DISPATCH_PROPERTYGET) != 0 && result != nullptr) {
        return MfcOleDispatchRuntimeTail_005fb0e0(&target, entry, *result);
    }
    if ((flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0 &&
        params != nullptr && params->cArgs > 0) {
        HRESULT hr = MfcOleDispatchRuntimeTail_005fb2c4(&target, entry,
            params->rgvarg[0]);
        if (SUCCEEDED(hr) && entry.on_changed != nullptr) {
            entry.on_changed(target);
        }
        return hr;
    }
    return DISP_E_MEMBERNOTFOUND;
}

HRESULT MfcOleDispatchRuntimeTail_005fc7a1(std::vector<VARIANTARG>& values,
    VARIANTARG* result, const VARIANTARG* source) {
    for (VARIANTARG& value : values) {
        ClearOleVariant(value);
    }
    values.clear();
    if (result != nullptr && source != nullptr) {
        return CopyOleVariant(*result, *source);
    }
    return S_OK;
}

ULONG MfcOleDispatchRuntimeTail_005fc9c4(void* inner_this) {
    auto& inner = *reinterpret_cast<MfcOleInnerUnknownCompat*>(
        static_cast<unsigned char*>(inner_this) - 0x10);
    return MfcOleRuntime_005f6e97(inner);
}

ULONG MfcOleDispatchRuntimeTail_005fc9df(void* inner_this) {
    auto& inner = *reinterpret_cast<MfcOleInnerUnknownCompat*>(
        static_cast<unsigned char*>(inner_this) - 0x10);
    return MfcOleRuntime_005f6f34(inner);
}

HRESULT MfcOleDispatchRuntimeTail_005fca42(ITypeInfo* type_info, UINT index,
    LCID locale, ITypeInfo** result) {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;
    if (index != 0) {
        return E_INVALIDARG;
    }
    if (type_info == nullptr) {
        return E_NOTIMPL;
    }
    (void)locale;
    type_info->AddRef();
    *result = type_info;
    return S_OK;
}

HRESULT MfcOleDispatchRuntimeTail_005fcac9(MfcCommandTargetCompat& target,
    REFIID iid, LPOLESTR* names, UINT name_count, LCID locale,
    DISPID* dispatch_ids) {
    if (dispatch_ids == nullptr || names == nullptr || name_count == 0) {
        return E_POINTER;
    }
    if (!IsEqualGUID(iid, IID_NULL)) {
        return DISP_E_UNKNOWNINTERFACE;
    }
    (void)locale;
    const std::string ansi_name = WideToAnsiStringCompat(names[0]);
    const DISPID dispatch_id = MfcOleDispatchRuntimeTail_005fb612(
        DispatchMapFromTarget(target), ansi_name.c_str());
    dispatch_ids[0] = dispatch_id;
    for (UINT index = 1; index < name_count; ++index) {
        dispatch_ids[index] = DISPID_UNKNOWN;
    }
    return dispatch_id == DISPID_UNKNOWN ? DISP_E_UNKNOWNNAME : S_OK;
}

HRESULT MfcOleDispatchRuntimeTail_005fccbc(MfcCommandTargetCompat& target,
    DISPID dispatch_id, REFIID iid, LCID locale, WORD flags,
    DISPPARAMS* params, VARIANTARG* result, EXCEPINFO* exception_info,
    UINT* arg_error) {
    if (!IsEqualGUID(iid, IID_NULL)) {
        return DISP_E_UNKNOWNINTERFACE;
    }
    (void)locale;
    if (!CmdTargetIsInvokeAllowed()) {
        return E_UNEXPECTED;
    }
    const MfcOleDispatchMapEntryCompat* entry =
        MfcOleDispatchRuntimeTail_005fb717(target, dispatch_id);
    if (entry == nullptr) {
        return DISP_E_MEMBERNOTFOUND;
    }
    return MfcOleDispatchRuntimeTail_005fc3cc(target, *entry, flags, result,
        params, exception_info, arg_error);
}

HRESULT MfcOleDispatchRuntimeTail_005fd45e(HRESULT status, UINT arg_error,
    UINT* out_arg_error) {
    if (FAILED(status) && out_arg_error != nullptr && arg_error != UINT_MAX) {
        *out_arg_error = arg_error;
    }
    return status;
}

void MfcOleDispatchRuntimeTail_005fd5b5(
    MfcOleDispatchExceptionCompat& exception) {
    exception.description.clear();
    exception.source.clear();
    exception.help_file.clear();
    DestroySimpleException(exception);
}

void MfcOleDispatchRuntimeTail_005fd627(EXCEPINFO& info,
    const MfcOleDispatchExceptionCompat& exception) {
    std::memset(&info, 0, sizeof(info));
    info.wCode = exception.code;
    info.scode = exception.scode;
    info.dwHelpContext = exception.help_context;
    info.bstrDescription = MfcOleDispatchRuntimeTail_005fba6b(
        exception.description.c_str());
    info.bstrSource = MfcOleDispatchRuntimeTail_005fba6b(
        exception.source.empty() ? AfxGetAppNameCompat() : exception.source.c_str());
    if (!exception.help_file.empty()) {
        info.bstrHelpFile = MfcOleDispatchRuntimeTail_005fba6b(
            exception.help_file.c_str());
    }
}

MfcOleDispatchExceptionCompat* MfcOleDispatchRuntimeTail_005fda57(
    MfcOleDispatchExceptionCompat* exception, const char* description,
    UINT help_id, WORD code) {
    if (exception == nullptr) {
        return nullptr;
    }
    ConstructSimpleException(*exception);
    exception->description = description == nullptr ? "" : description;
    exception->help_context = help_id == 0 ? 0 : help_id + 0x60000U;
    exception->code = code;
    exception->source = AfxGetAppNameCompat();
    exception->scode = code == 0 ? E_FAIL : DISP_E_EXCEPTION;
    return exception;
}

BOOL MfcOleDispatchRuntimeTail_005fdb37(
    const MfcOleDispatchExceptionCompat& exception, LPSTR buffer,
    int max_chars, DWORD* help_context) {
    if (help_context != nullptr) {
        *help_context = exception.help_context;
    }
    if (buffer == nullptr || max_chars <= 0) {
        return FALSE;
    }
    lstrcpynA(buffer, exception.description.c_str(), max_chars);
    return TRUE;
}

[[noreturn]] void MfcOleDispatchRuntimeTail_005fdba9(WORD code,
    const char* description, UINT help_id) {
    MfcOleDispatchExceptionCompat exception;
    MfcOleDispatchRuntimeTail_005fda57(&exception, description, help_id, code);
    throw std::runtime_error(exception.description);
}

[[noreturn]] void MfcOleDispatchRuntimeTail_005fdc66(WORD code,
    UINT string_id, int help_id) {
    char text[256]{};
    if (AfxLoadStringCompat(string_id, text, sizeof(text)) == 0) {
        std::snprintf(text, sizeof(text), "Dispatch exception %u",
            static_cast<unsigned>(code));
    }
    MfcOleDispatchRuntimeTail_005fdba9(code, text,
        help_id < 0 ? string_id : static_cast<UINT>(help_id));
}

namespace {

struct MfcOleInterfaceAdapterCompat {
    void* owner = nullptr;
    IUnknown* external_unknown = nullptr;
};

using MfcOleInterfaceInvokeCallback = HRESULT (*)(void* context,
    void* argument_frame, int frame_bytes);

} // namespace

void* MfcOleInterfaceRuntime_005fdda0(void* object);
void* MfcOleInterfaceRuntime_005fddc0(void* object);
void* MfcOleInterfaceRuntime_005fdde0(void* object);

MfcOleInterfaceAdapterCompat* MfcOleInterfaceRuntime_005fdd60(
    MfcOleInterfaceAdapterCompat* adapter, void* owner) {
    if (adapter != nullptr) {
        MfcOleInterfaceRuntime_005fdda0(adapter);
        adapter->owner = owner;
        adapter->external_unknown = nullptr;
    }
    return adapter;
}

MfcOleInterfaceAdapterCompat* MfcOleInterfaceRuntime_005fdd80(
    MfcOleInterfaceAdapterCompat* adapter) {
    MfcOleInterfaceRuntime_005fddc0(adapter);
    return adapter;
}

void* MfcOleInterfaceRuntime_005fdda0(void* object) {
    return object;
}

void* MfcOleInterfaceRuntime_005fddc0(void* object) {
    return MfcOleInterfaceRuntime_005fdde0(object);
}

void* MfcOleInterfaceRuntime_005fdde0(void* object) {
    return object;
}

MfcOleDispatchExceptionCompat* MfcOleInterfaceRuntime_005fddf0(
    MfcOleDispatchExceptionCompat* exception, unsigned flags) {
    if (exception == nullptr) {
        return nullptr;
    }
    MfcOleDispatchRuntimeTail_005fd5b5(*exception);
    if ((flags & 1U) != 0U) {
        MfcExceptionDestroyStorage(exception, true);
    }
    return exception;
}

HRESULT MfcOleInterfaceRuntime_005fde20(
    MfcOleInterfaceInvokeCallback callback, void* context, void* argument_frame,
    int frame_bytes) {
    if (callback == nullptr) {
        return E_POINTER;
    }
    return callback(context, argument_frame, frame_bytes);
}

namespace {

struct MfcThreadSlotDataCompat {
    int next_slot = 1;
    std::vector<int> free_slots;
};

using CThreadSlotData = MfcThreadSlotDataCompat;
using CThreadLocalObject = MfcThreadLocalObjectCompat;

std::unordered_map<int, void*>& ThreadSlotValues() {
    static thread_local std::unordered_map<int, void*> values;
    return values;
}

MfcThreadSlotDataCompat& GlobalThreadSlotDataCompat() {
    static MfcThreadSlotDataCompat data;
    return data;
}

std::vector<unsigned>& ThreadLocalLockCounts() {
    static std::vector<unsigned> counts(32);
    return counts;
}

} // namespace

LPWSTR MfcThreadSlotRuntime_005eaa16(LPCSTR text);
LPSTR MfcThreadSlotRuntime_005eaa90(LPCWSTR text);

void MfcThreadSlotRuntime_005ea500(std::string& target, BSTR source) {
    target = WideToAnsiStringCompat(source);
}

BSTR MfcThreadSlotRuntime_005ea5a3(BSTR source) {
    return SysAllocString(source);
}

LPWSTR MfcThreadSlotRuntime_005ea62b(LPVOID text) {
    LPWSTR converted = MfcThreadSlotRuntime_005eaa16(static_cast<LPCSTR>(text));
    CoTaskMemFree(text);
    return converted;
}

LPSTR MfcThreadSlotRuntime_005ea64e(LPVOID text) {
    LPSTR converted = MfcThreadSlotRuntime_005eaa90(static_cast<LPCWSTR>(text));
    CoTaskMemFree(text);
    return converted;
}

int MfcThreadSlotRuntime_005ea671(LPCSTR text, int chars) {
    return MultiByteToWideChar(CP_ACP, 0, text, chars, nullptr, 0);
}

int MfcThreadSlotRuntime_005ea73e(LPCWSTR text, int chars) {
    return WideCharToMultiByte(CP_ACP, 0, text, chars, nullptr, 0, nullptr,
        nullptr);
}

int MfcThreadSlotRuntime_005ea80b(LPCSTR text, int chars) {
    return MfcThreadSlotRuntime_005ea671(text, chars);
}

int MfcThreadSlotRuntime_005ea8df(LPCWSTR text, int chars) {
    return MfcThreadSlotRuntime_005ea73e(text, chars);
}

LPVOID MfcThreadSlotRuntime_005ea9c3(SIZE_T bytes) {
    return bytes == 0 ? nullptr : LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, bytes);
}

LPWSTR MfcThreadSlotRuntime_005eaa16(LPCSTR text) {
    static thread_local std::wstring wide;
    wide = AnsiToWideStringCompat(text);
    return wide.empty() ? nullptr : wide.data();
}

LPSTR MfcThreadSlotRuntime_005eaa90(LPCWSTR text) {
    static thread_local std::string ansi;
    ansi = WideToAnsiStringCompat(text);
    return ansi.empty() ? nullptr : ansi.data();
}

LPVOID MfcThreadSlotRuntime_005eab0f(LPCSTR text) {
    if (text == nullptr) {
        return nullptr;
    }
    const SIZE_T bytes = lstrlenA(text) + 1;
    auto memory = static_cast<char*>(
        MfcThreadSlotRuntime_005ea9c3(bytes));
    if (memory != nullptr) {
        std::memcpy(memory, text, bytes);
    }
    return memory;
}

void MfcThreadSlotRuntime_005eab60(int slot) {
    ThreadSlotValues().erase(slot);
}

void* MfcThreadSlotRuntime_005eabd3(int slot) {
    auto& values = ThreadSlotValues();
    auto found = values.find(slot);
    return found == values.end() ? nullptr : found->second;
}

void MfcThreadSlotRuntime_005eacd7(HLOCAL memory) {
    if (memory != nullptr) {
        LocalFree(memory);
    }
}

void MfcThreadSlotRuntime_005ead15(HLOCAL memory) {
    if (memory != nullptr) {
        LocalFree(memory);
    }
}

MfcThreadSlotDataCompat* MfcThreadSlotRuntime_005eada1(
    MfcThreadSlotDataCompat* data) {
    if (data == nullptr) {
        data = &GlobalThreadSlotDataCompat();
    }
    data->next_slot = std::max(data->next_slot, 1);
    return data;
}

int MfcThreadSlotRuntime_005eae42(MfcThreadSlotDataCompat* data) {
    if (data == nullptr) {
        data = &GlobalThreadSlotDataCompat();
    }
    if (!data->free_slots.empty()) {
        const int slot = data->free_slots.back();
        data->free_slots.pop_back();
        return slot;
    }
    return data->next_slot++;
}

void MfcThreadSlotRuntime_005eb037(MfcThreadSlotDataCompat*, int slot) {
    ThreadSlotValues().erase(slot);
}

void MfcThreadSlotRuntimeDeleteSlot(int slot) {
    if (slot != 0) {
        MfcThreadSlotRuntime_005eb037(&GlobalThreadSlotDataCompat(), slot);
    }
}

void MfcThreadSlotRuntime_005eb169(MfcThreadSlotDataCompat*, int slot,
    void* value) {
    if (value == nullptr) {
        ThreadSlotValues().erase(slot);
    } else {
        ThreadSlotValues()[slot] = value;
    }
}

void MfcThreadSlotRuntime_005eb3a8(MfcThreadSlotDataCompat* data, int slot) {
    ThreadSlotValues().erase(slot);
    if (data == nullptr) {
        data = &GlobalThreadSlotDataCompat();
    }
    data->free_slots.push_back(slot);
}

void MfcThreadSlotRuntime_005eb46c(MfcThreadSlotDataCompat*, HINSTANCE,
    bool delete_all = false) {
    if (delete_all) {
        ThreadSlotValues().clear();
    }
}

int MfcThreadSlotRuntime_005eb63c(int* slot,
    MfcCreateObjectCallback create_object) {
    if (slot == nullptr) {
        return 0;
    }
    if (*slot == 0) {
        *slot = MfcThreadSlotRuntime_005eae42(&GlobalThreadSlotDataCompat());
    }
    void* value = MfcThreadSlotRuntime_005eabd3(*slot);
    if (value == nullptr && create_object != nullptr) {
        value = create_object();
        MfcThreadSlotRuntime_005eb169(&GlobalThreadSlotDataCompat(), *slot,
            value);
    }
    return value != nullptr;
}

int MfcThreadSlotRuntime_005eb7c4(int* slot,
    MfcCreateObjectCallback create_object) {
    return MfcThreadSlotRuntime_005eb63c(slot, create_object);
}

void* MfcThreadSlotRuntime_005eb852() {
    return &GlobalThreadSlotDataCompat();
}

void MfcThreadSlotRuntime_005eb878(int* slot) {
    if (slot != nullptr && *slot != 0) {
        MfcThreadSlotRuntime_005eb3a8(&GlobalThreadSlotDataCompat(), *slot);
        *slot = 0;
    }
}

void MfcThreadSlotRuntime_005eb8bf(HINSTANCE instance) {
    MfcThreadSlotRuntime_005eb46c(&GlobalThreadSlotDataCompat(), instance, true);
}

void AfxTermLocalDataCompat(HINSTANCE instance, bool delete_all) {
    MfcThreadSlotRuntime_005eb46c(&GlobalThreadSlotDataCompat(), instance,
        delete_all);
}

namespace {

int& ThreadLocalRuntimeRefCount() {
    static int value = 0;
    return value;
}

bool& ThreadLocalRuntimeInitialized() {
    static bool value = false;
    return value;
}

} // namespace

void MfcThreadLocalRuntime_005eb901() {
    ++ThreadLocalRuntimeRefCount();
    ThreadLocalRuntimeInitialized() = true;
}

void MfcThreadLocalRuntime_005eb913() {
    int& refs = ThreadLocalRuntimeRefCount();
    if (refs > 0) {
        --refs;
    }
    if (refs == 0) {
        ThreadLocalRuntimeInitialized() = false;
        ThreadSlotValues().clear();
    }
}

void MfcThreadLocalRuntime_005eb9b0(MfcThreadLocalObjectCompat* local);

MfcThreadLocalObjectCompat* MfcThreadLocalRuntime_005eb960(
    MfcThreadLocalObjectCompat* local) {
    if (local != nullptr) {
        local->data = nullptr;
    }
    return local;
}

MfcThreadLocalObjectCompat* MfcThreadLocalRuntime_005eb980(
    MfcThreadLocalObjectCompat* local, unsigned flags) {
    MfcThreadLocalRuntime_005eb9b0(local);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteNormalBlock(local);
    }
    return local;
}

void MfcThreadLocalRuntime_005eb9b0(MfcThreadLocalObjectCompat* local) {
    if (local != nullptr) {
        local->data = nullptr;
    }
}

void* MfcThreadLocalRuntime_005eb9d0(int slot) {
    return MfcThreadSlotRuntime_005eabd3(slot);
}

MfcThreadSlotDataCompat* MfcThreadLocalRuntime_005ebac0(
    MfcThreadSlotDataCompat* data, unsigned flags = 0) {
    MfcThreadSlotDataCompat* const original = data;
    MfcThreadSlotRuntime_005eada1(data);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteNormalBlock(original);
    }
    return original;
}

MfcSimpleListCompat* MfcThreadLocalRuntime_005ebaf0(
    MfcSimpleListCompat* list, int element_size) {
    if (list != nullptr) {
        list->element_size = element_size;
        list->head = nullptr;
    }
    return list;
}

void MfcThreadLocalRuntime_005ebb10(int slot) {
    MfcThreadSlotRuntime_005eab60(slot);
}

void* MfcThreadLocalRuntime_005ebb30(int slot) {
    return MfcThreadSlotRuntime_005eabd3(slot);
}

void* GetDataNA(int slot) {
    return MfcThreadLocalRuntime_005ebb30(slot);
}

MfcThreadSlotDataCompat* MfcThreadLocalRuntime_005ebb50() {
    return &GlobalThreadSlotDataCompat();
}

void** MfcThreadLocalRuntime_005ebb70() {
    static void* runtime_class_token = nullptr;
    return &runtime_class_token;
}

int MfcThreadLocalRuntime_005ebb80() {
    ThreadLocalRuntimeInitialized() = true;
    return 1;
}

void MfcThreadLocalRuntime_005ebc1b() {
    ThreadLocalRuntimeInitialized() = false;
    for (unsigned& count : ThreadLocalLockCounts()) {
        count = 0;
    }
}

void MfcThreadLocalRuntime_005ebd1a(UINT lock_index) {
    if (!ThreadLocalRuntimeInitialized()) {
        MfcThreadLocalRuntime_005ebb80();
    }
    std::vector<unsigned>& counts = ThreadLocalLockCounts();
    if (lock_index >= counts.size()) {
        counts.resize(lock_index + 1);
    }
    ++counts[lock_index];
}

void MfcThreadLocalRuntime_005ebe4c(UINT lock_index) {
    std::vector<unsigned>& counts = ThreadLocalLockCounts();
    if (lock_index < counts.size() && counts[lock_index] > 0) {
        --counts[lock_index];
    }
}

void MfcThreadLocalRuntime_005ebf20() {
    g_temp_map_lock_count = 0;
}

void MfcThreadLocalRuntime_005ec1ce(MfcObjectCompat* object) {
    CObjectAssertValid(object);
}

void** MfcThreadLocalRuntime_005ec1e1() {
    static void* runtime_class_token = nullptr;
    return &runtime_class_token;
}

void MfcThreadLocalRuntime_005ec200(MfcOleStateCompat* state) {
    if (state == nullptr) {
        return;
    }
    if (state->cleanup != nullptr) {
        state->cleanup(state->cleanup_context);
        state->cleanup = nullptr;
    }
    if (state->library != nullptr) {
        FreeLibrary(state->library);
        state->library = nullptr;
    }
}

void MfcThreadLocalRuntime_005ec270(MfcAmbientCacheCompat* cache) {
    if (cache != nullptr) {
        cache->cached_object = nullptr;
    }
}

BOOL MfcThreadLocalRuntime_005ec2dd() {
    return TRUE;
}

void WinAppDoWaitCursor(MfcWinAppCompat& app, int code) {
    if (code != -1 && code != 0 && code != 1) {
        CrtDbgReport(2, "appui.cpp", 0x34, nullptr,
            "invalid CWinApp::DoWaitCursor code");
        return;
    }
    HCURSOR wait_cursor = LoadCursorA(nullptr, IDC_WAIT);
    if (wait_cursor == nullptr) {
        return;
    }
    if (code > 0) {
        HCURSOR previous = SetCursor(wait_cursor);
        ++app.wait_cursor_count;
        if (app.wait_cursor_count == 1) {
            app.wait_cursor_restore = previous;
        }
    } else if (code < 0) {
        --app.wait_cursor_count;
        if (app.wait_cursor_count < 1) {
            app.wait_cursor_count = 0;
            SetCursor(app.wait_cursor_restore);
        }
    } else if (app.wait_cursor_count > 0) {
        SetCursor(wait_cursor);
    }
    g_wait_cursor_count = app.wait_cursor_count;
    g_previous_wait_cursor = app.wait_cursor_restore;
}

bool WinAppSaveAllModified(MfcWinAppCompat& app) {
    auto* manager = static_cast<MfcDocManagerCompat*>(app.doc_manager);
    if (manager == nullptr) {
        return true;
    }
    for (MfcDocTemplateCompat* templ : manager->templates) {
        if (templ != nullptr && !DocTemplateSaveAllModified(*templ)) {
            return false;
        }
    }
    return true;
}

void WinAppSetAppId(MfcWinAppCompat& app, const char* app_id) {
    if (app_id == nullptr) {
        CrtDbgReport(2, "appui.cpp", 0x52, nullptr,
            "CWinApp app-id string is null");
        return;
    }
    app.app_id = app_id;
}

void WinAppAddDocTemplate(MfcWinAppCompat& app, MfcDocTemplateCompat* templ) {
    if (templ == nullptr) {
        return;
    }
    if (app.doc_manager == nullptr) {
        auto* manager = new MfcDocManagerCompat();
        ConstructDocManager(*manager);
        app.doc_manager = manager;
    }
    DocManagerAddDocTemplate(*static_cast<MfcDocManagerCompat*>(app.doc_manager),
        *templ);
}

void WinAppRemoveDocTemplate(MfcWinAppCompat& app, MfcDocTemplateCompat* templ) {
    if (app.doc_manager == nullptr || templ == nullptr) {
        return;
    }
    DocManagerRemoveDocTemplate(*static_cast<MfcDocManagerCompat*>(app.doc_manager),
        *templ);
}

void WinAppRouteHelpCommand(MfcWinAppCompat& app, void (*fallback)(void*),
    void* context) {
    AfxAssertValidObject(&app, "appui.cpp", 0x6a);
    if (app.help_manager != nullptr) {
        if (app.help_manager->route_help_command != nullptr) {
            app.help_manager->route_help_command(*app.help_manager,
                fallback, context);
            return;
        }
        AfxTraceOutput("Warning: help manager has no route callback.\n");
        return;
    }
    if (fallback != nullptr) {
        fallback(context);
    }
}

bool WinAppPromptFileName(MfcWinAppCompat& app, MfcCStringCompat& path,
    UINT title_id, DWORD flags, bool open_dialog, MfcDocTemplateCompat* templ) {
    if (app.doc_manager == nullptr) {
        return false;
    }
    std::string selected = path.text;
    if (!DocManagerDoPromptFileName(
            *static_cast<MfcDocManagerCompat*>(app.doc_manager), selected,
            title_id, flags, open_dialog, templ)) {
        return false;
    }
    path.text = selected;
    return true;
}

namespace {

std::string win_app_profile_path(const MfcWinAppCompat& app) {
    std::string profile = app.profile_name.empty() ? "ranker.ini" : app.profile_name;
    if (profile.find(':') == std::string::npos &&
        profile.find('\\') == std::string::npos &&
        profile.find('/') == std::string::npos &&
        profile.find('.') == std::string::npos) {
        profile += ".ini";
    }
    return profile;
}

bool win_app_open_profile_key(const MfcWinAppCompat& app, const char* section,
    HKEY& key) {
    key = nullptr;
    if (section == nullptr) {
        return false;
    }
    std::string subkey = "Software\\";
    subkey += app.registry_key.empty() ? app.app_name : app.registry_key;
    if (!subkey.empty() && subkey.back() != '\\') {
        subkey += '\\';
    }
    subkey += section;
    return RegCreateKeyExA(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS;
}

} // namespace

bool WinAppOpenRecentFile(MfcWinAppCompat& app, UINT command_id) {
    MfcRecentFileListCompat* recent = app.recent_file_list;
    if (recent == nullptr) {
        return false;
    }
    const UINT start = recent->start != 0 ? recent->start : 0xe110U;
    if (command_id < start) {
        return false;
    }
    const int index = static_cast<int>(command_id - start);
    if (index < 0 || index >= static_cast<int>(recent->names.size()) ||
        recent->names[static_cast<std::size_t>(index)].empty()) {
        return false;
    }
    const std::string path = recent->names[static_cast<std::size_t>(index)];
    AfxTraceOutput("MRU: open file %u: %s\n", command_id - start + 1,
        path.c_str());
    MfcDocumentCompat* opened = app.doc_manager == nullptr
        ? DocManagerOpenDocumentFile(path.c_str())
        : DocManagerOpenDocumentFile(
              static_cast<MfcDocManagerCompat*>(app.doc_manager), path.c_str());
    if (opened == nullptr) {
        RecentFileListRemove(*recent, index);
    }
    return true;
}

bool WinAppWriteProfileInt(MfcWinAppCompat& app, const char* section,
    const char* entry, int value) {
    if (section == nullptr || entry == nullptr) {
        return false;
    }
    if (!app.use_registry) {
        char text[32]{};
        wsprintfA(text, "%d", value);
        const std::string profile = win_app_profile_path(app);
        return WritePrivateProfileStringA(section, entry, text,
            profile.c_str()) != FALSE;
    }
    HKEY key = nullptr;
    if (!win_app_open_profile_key(app, section, key)) {
        return false;
    }
    const DWORD data = static_cast<DWORD>(value);
    const LSTATUS status = RegSetValueExA(key, entry, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool WinAppWriteProfileString(MfcWinAppCompat& app, const char* section,
    const char* entry, const char* value) {
    if (section == nullptr) {
        return false;
    }
    if (!app.use_registry) {
        const std::string profile = win_app_profile_path(app);
        return WritePrivateProfileStringA(section, entry, value,
            profile.c_str()) != FALSE;
    }
    if (entry == nullptr) {
        std::string subkey = "Software\\";
        subkey += app.registry_key.empty() ? app.app_name : app.registry_key;
        subkey += "\\";
        subkey += section;
        return RegDeleteKeyA(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS;
    }
    HKEY key = nullptr;
    if (!win_app_open_profile_key(app, section, key)) {
        return false;
    }
    LSTATUS status = ERROR_SUCCESS;
    if (value == nullptr) {
        status = RegDeleteValueA(key, entry);
    } else {
        status = RegSetValueExA(key, entry, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(value),
            static_cast<DWORD>(lstrlenA(value) + 1));
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool WinAppWriteProfileBinary(MfcWinAppCompat& app, const char* section,
    const char* entry, const BYTE* data, UINT bytes) {
    if (section == nullptr || entry == nullptr ||
        (data == nullptr && bytes != 0)) {
        return false;
    }
    if (app.use_registry) {
        HKEY key = nullptr;
        if (!win_app_open_profile_key(app, section, key)) {
            return false;
        }
        const LSTATUS status = RegSetValueExA(key, entry, 0, REG_BINARY,
            data, bytes);
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
    std::string encoded;
    encoded.reserve(static_cast<std::size_t>(bytes) * 2U);
    for (UINT i = 0; i < bytes; ++i) {
        encoded.push_back(static_cast<char>('A' + (data[i] & 0x0f)));
        encoded.push_back(static_cast<char>('A' + ((data[i] >> 4) & 0x0f)));
    }
    return WinAppWriteProfileString(app, section, entry, encoded.c_str());
}

HWND AfxGetMainWndHandleCompat() {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->main_window != nullptr) {
        return thread->main_window;
    }
    if (g_app_win_thread != nullptr) {
        return g_app_win_thread->main_window;
    }
    return nullptr;
}

MfcCWndCompat* AfxGetMainWndCompat() {
    HWND main_window = AfxGetMainWndHandleCompat();
    return main_window == nullptr ? nullptr : CWndFromHandle(main_window);
}

void AfxEnableModelessCompat(bool enable) {
    MfcCWndCompat* main = AfxGetMainWndCompat();
    if (main == nullptr || main->window == nullptr || !IsWindow(main->window)) {
        return;
    }
    if (!enable && !IsWindowEnabled(main->window)) {
        return;
    }
    AfxTraceOutput("AfxEnableModeless(%d)\n", enable ? 1 : 0);
}

HWND AfxGetSafeOwnerCompat(HWND parent, HWND* disabled_owner) {
    HWND owner = parent;
    if (owner == nullptr) {
        MfcCWndCompat* routing =
            static_cast<MfcCWndCompat*>(GetThreadStateRoutingFrameSlot());
        if (routing != nullptr && routing->window != nullptr) {
            owner = routing->window;
        }
        if (owner == nullptr) {
            owner = AfxGetMainWndHandleCompat();
        }
    }

    while (owner != nullptr &&
        (GetWindowLongA(owner, GWL_STYLE) & WS_CHILD) != 0) {
        owner = GetParent(owner);
    }

    HWND top = owner;
    for (HWND cursor = owner; cursor != nullptr; cursor = GetParent(cursor)) {
        top = cursor;
    }
    if (parent == nullptr && owner != nullptr) {
        owner = GetLastActivePopup(owner);
    }

    if (disabled_owner != nullptr) {
        if (top != nullptr && top != owner && IsWindowEnabled(top)) {
            *disabled_owner = top;
            EnableWindow(top, FALSE);
        } else {
            *disabled_owner = nullptr;
        }
    }
    return owner;
}

int WinAppDoMessageBox(MfcWinAppCompat* app, const char* prompt,
    UINT type, UINT id_prompt) {
    AfxEnableModelessCompat(false);

    HWND disabled_owner = nullptr;
    HWND owner = AfxGetSafeOwnerCompat(nullptr, &disabled_owner);

    ULONG_PTR old_prompt_context = 0;
    bool prompt_context_saved = false;
    if (app != nullptr && id_prompt != 0) {
        old_prompt_context = app->prompt_context;
        app->prompt_context = static_cast<ULONG_PTR>(id_prompt) + 0x30000U;
        prompt_context_saved = true;
    }

    if ((type & MB_ICONMASK) == 0) {
        switch (type & MB_TYPEMASK) {
        case MB_OK:
        case MB_OKCANCEL:
        case MB_YESNOCANCEL:
        case MB_YESNO:
            type |= MB_ICONEXCLAMATION;
            break;
        default:
            break;
        }
    }
    if ((type & MB_ICONMASK) == 0) {
        AfxTraceOutput("Warning: no icon specified for message box.\n");
    }

    char module_name[MAX_PATH]{};
    const char* caption = nullptr;
    if (app != nullptr && !app->app_name.empty()) {
        caption = app->app_name.c_str();
    } else {
        GetModuleFileNameA(nullptr, module_name,
            static_cast<DWORD>(sizeof(module_name)));
        caption = module_name[0] == '\0' ? "Ranker" : module_name;
    }

    const int result = MessageBoxA(owner, prompt == nullptr ? "" : prompt,
        caption, type);

    if (prompt_context_saved) {
        app->prompt_context = old_prompt_context;
    }
    if (disabled_owner != nullptr) {
        EnableWindow(disabled_owner, TRUE);
    }
    AfxEnableModelessCompat(true);
    return result;
}

int AfxMessageBoxCompat(const char* prompt, UINT type, UINT id_help) {
    return WinAppDoMessageBox(AfxGetAppCompat(), prompt, type, id_help);
}

int AfxMessageBoxResource(UINT id_prompt, UINT type, UINT id_help) {
    MfcCStringCompat text;
    if (!CStringLoadString(text, id_prompt)) {
        AfxTraceOutput("Error: failed to load message box prompt 0x%04x.\n",
            id_prompt);
        char fallback[64]{};
        std::snprintf(fallback, sizeof(fallback), "Message 0x%04x",
            id_prompt);
        text.text = fallback;
    }
    if (id_help == 0xffffffffU) {
        id_help = id_prompt;
    }
    return AfxMessageBoxCompat(text.text.c_str(), type, id_help);
}

MfcWinThreadCompat* AfxBeginThreadProc(unsigned (__stdcall *thread_proc)(void*),
    void* params, int priority, unsigned stack_size, unsigned create_flags,
    LPSECURITY_ATTRIBUTES security_attributes) {
    if (thread_proc == nullptr) {
        return nullptr;
    }

    auto* thread = new MfcWinThreadCompat();
    thread->runtime_class = GetCWinThreadRuntimeClass();
    thread->thread_proc = thread_proc;
    thread->thread_params = params;
    thread->auto_delete = true;

    if (!CreateWinThreadHandle(*thread, create_flags, stack_size,
        security_attributes)) {
        delete thread;
        return nullptr;
    }

    if (priority != THREAD_PRIORITY_NORMAL && thread->thread != nullptr) {
        SetThreadPriority(thread->thread, priority);
    }
    if ((create_flags & CREATE_SUSPENDED) == 0 && thread->thread != nullptr) {
        ResumeThread(thread->thread);
    }
    return thread;
}

MfcWinThreadCompat* AfxBeginThreadRuntimeClass(
    MfcRuntimeClassCompat& runtime_class, int priority, unsigned stack_size,
    unsigned create_flags, LPSECURITY_ATTRIBUTES security_attributes) {
    void* object = RuntimeClassCreateObject(runtime_class);
    if (object == nullptr) {
        ThrowMfcMemoryException();
    }

    auto* thread = static_cast<MfcWinThreadCompat*>(object);
    thread->runtime_class = &runtime_class;
    thread->auto_delete = true;
    thread->thread_proc = nullptr;
    thread->thread_params = nullptr;

    if (!CreateWinThreadHandle(*thread, create_flags, stack_size,
        security_attributes)) {
        delete thread;
        return nullptr;
    }

    if (priority != THREAD_PRIORITY_NORMAL && thread->thread != nullptr) {
        SetThreadPriority(thread->thread, priority);
    }
    if ((create_flags & CREATE_SUSPENDED) == 0 && thread->thread != nullptr) {
        ResumeThread(thread->thread);
    }
    return thread;
}

void AfxEndThreadCompat(unsigned exit_code, bool delete_thread,
    MfcWinThreadCompat* current_thread) {
    if (current_thread == nullptr) {
        current_thread = g_current_win_thread;
    }
    if (current_thread != nullptr) {
        if (g_current_win_thread == current_thread) {
            g_current_win_thread = nullptr;
        }
        current_thread->thread = nullptr;
        if (delete_thread) {
            delete current_thread;
        }
    }
    ExitThread(exit_code);
}

void AfxWinInitThread(MfcWinThreadCompat& thread) {
    thread.current_message = MSG{};
    thread.cursor_last = POINT{};
    thread.message_last = 0;
    g_current_win_thread = &thread;
    if (thread.runtime_class == nullptr) {
        thread.runtime_class = GetCWinThreadRuntimeClass();
    }
}

void AfxTermThread(HINSTANCE instance) {
    (void)instance;
    if (g_current_win_thread != nullptr) {
        g_current_win_thread->disable_pump_count = 0;
    }
}

void AfxTermThreadEpilogue() {
}

bool CreateWinThreadHandle(MfcWinThreadCompat& thread, unsigned create_flags,
    unsigned stack_size, LPSECURITY_ATTRIBUTES security_attributes) {
    if (thread.thread != nullptr) {
        return false;
    }

    DWORD thread_id = 0;
    thread.thread = CrtBeginThreadEx(security_attributes, stack_size,
        WinThreadStartThunk, &thread, create_flags | CREATE_SUSPENDED,
        &thread_id);
    if (thread.thread == nullptr) {
        thread.thread_id = 0;
        return false;
    }
    thread.thread_id = static_cast<unsigned>(thread_id);
    return true;
}

int WinThreadInitInstance(MfcWinThreadCompat& thread) {
    AfxAssertValidObject(&thread, "thrdcore.cpp", 0x1c9);
    return 0;
}

int WinThreadRun(MfcWinThreadCompat& thread) {
    AfxAssertValidObject(&thread, "thrdcore.cpp", 0x1d1);
    bool do_idle = true;
    long idle_count = 0;

    for (;;) {
        while (do_idle &&
            PeekMessageA(&thread.current_message, nullptr, 0, 0, PM_NOREMOVE) == 0) {
            if (!WinThreadOnIdle(thread, idle_count++)) {
                do_idle = false;
            }
        }

        do {
            if (!WinThreadPumpMessage(thread)) {
                return static_cast<int>(WinThreadExitInstance(thread));
            }
            if (WinThreadIsIdleMessage(thread, thread.current_message)) {
                do_idle = true;
                idle_count = 0;
            }
        } while (PeekMessageA(&thread.current_message, nullptr, 0, 0,
            PM_NOREMOVE) != 0);
    }
}

bool WinThreadIsIdleMessage(MfcWinThreadCompat& thread, const MSG& message) {
    if (message.message == WM_MOUSEMOVE || message.message == WM_NCMOUSEMOVE) {
        if (thread.message_last == message.message &&
            thread.cursor_last.x == message.pt.x &&
            thread.cursor_last.y == message.pt.y) {
            return false;
        }
        thread.message_last = message.message;
        thread.cursor_last = message.pt;
        return true;
    }
    if (message.message == WM_PAINT || message.message == WM_TIMER) {
        return false;
    }
    return true;
}

unsigned WinThreadExitInstance(MfcWinThreadCompat& thread) {
    AfxAssertValidObject(&thread, "thrdcore.cpp", 0x210);
    return static_cast<unsigned>(thread.current_message.wParam);
}

bool WinThreadOnIdle(MfcWinThreadCompat& thread, long count) {
    AfxAssertValidObject(&thread, "thrdcore.cpp", 0x219);
    if ((CrtSetDebugFlag(-1) & 4) != 0 && !AfxCheckMemoryCompat()) {
        CrtDbgReport(2, "thrdcore.cpp", 0x21e, nullptr,
            "memory check failed during OnIdle");
    }
    (void)thread;
    return count < 0;
}

bool AfxPreTranslateMessageThunk(MSG& msg) {
    return AfxInternalPreTranslateMessage(msg);
}

bool AfxInternalPreTranslateMessage(MSG& msg) {
    (void)msg;
    return false;
}

bool WinThreadPreTranslateMessage(MfcWinThreadCompat& thread, MSG& msg) {
    AfxAssertValidObject(&thread, "thrdcore.cpp", 0x298);
    if (AfxInternalPreTranslateMessage(msg)) {
        return true;
    }
    if (thread.main_window != nullptr && msg.hwnd != nullptr &&
        GetAncestor(msg.hwnd, GA_ROOT) != thread.main_window) {
        return IsDialogMessageA(thread.main_window, &msg) != 0;
    }
    return false;
}

bool WinThreadProcessMessageFilter(MfcWinThreadCompat& thread, int code,
    MSG& msg) {
    if (code < 0) {
        return false;
    }
    if (code == MSGF_MESSAGEBOX || code == MSGF_DIALOGBOX ||
        code == MSGF_MENU || code == MSGF_SCROLLBAR || code == MSGF_NEXTWINDOW) {
        return WinThreadPreTranslateMessage(thread, msg);
    }
    return false;
}

int WinThreadProcessWndProcException(MfcWinThreadCompat& thread,
    void* exception, MSG* message) {
    (void)thread;
    (void)exception;
    if (message == nullptr) {
        return 0;
    }
    if (message->message == WM_CREATE) {
        return -1;
    }
    if (message->message == WM_PAINT) {
        ValidateRect(message->hwnd, nullptr);
        return 0;
    }
    return 0;
}

LRESULT CALLBACK MfcKeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    auto* thread = AfxGetThreadCompat();
    auto* message = reinterpret_cast<MSG*>(lparam);
    if ((code >= 0 || code == MSGF_NEXTWINDOW) && thread != nullptr &&
        message != nullptr &&
        WinThreadProcessMessageFilterWithHelp(*thread, code, message)) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

bool IsHelpKeyMessage(const MSG& msg) {
    if (msg.message != WM_KEYDOWN || msg.wParam != VK_F1) {
        return false;
    }
    if ((static_cast<unsigned long>(msg.lParam) & 0x40000000UL) != 0) {
        return false;
    }
    return GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_CONTROL) >= 0 &&
        GetKeyState(VK_MENU) >= 0;
}

bool WinThreadProcessMessageFilterWithHelp(MfcWinThreadCompat& thread,
    int code, MSG* msg) {
    if (msg == nullptr) {
        return false;
    }
    if (IsHelpKeyMessage(*msg)) {
        HWND target = thread.main_window != nullptr ? thread.main_window : msg->hwnd;
        if (target != nullptr) {
            SendMessageA(target, WM_COMMAND, kMfcHelpCommand, 0);
        }
        return true;
    }
    return WinThreadProcessMessageFilter(thread, code, *msg);
}

bool WinThreadPumpMessage(MfcWinThreadCompat& thread) {
    AfxAssertValidObject(&thread, "thrdcore.cpp", 0x333);
    const BOOL result = GetMessageA(&thread.current_message, nullptr, 0, 0);
    if (result <= 0) {
        ++thread.quit_count;
        return false;
    }
    if (thread.quit_count != 0) {
        AfxTraceOutput("Error: CWinThread::PumpMessage called after WM_QUIT.\n");
        thread.quit_count = 0;
    }
    if (thread.current_message.message != kMfcKickIdleMessage &&
        !WinThreadPreTranslateMessage(thread, thread.current_message)) {
        TranslateMessage(&thread.current_message);
        DispatchMessageA(&thread.current_message);
    }
    return true;
}

void DetachThreadState(void* state) {
    if (g_current_win_thread == state) {
        g_current_win_thread = nullptr;
    }
}

void WinThreadDump(const MfcWinThreadCompat& thread) {
    AfxTraceOutput("m_pThreadParams = %p\n", thread.thread_params);
    AfxTraceOutput("m_pfnThreadProc = %p\n",
        reinterpret_cast<const void*>(thread.thread_proc));
    AfxTraceOutput("m_bAutoDelete = %d\n", thread.auto_delete ? 1 : 0);
    AfxTraceOutput("m_hThread = %p\n", thread.thread);
    AfxTraceOutput("m_nThreadID = %u\n", thread.thread_id);
    AfxTraceOutput("m_nDisablePumpCount = %d\n", thread.disable_pump_count);
    AfxTraceOutput("m_msgCur.hwnd = %p\n", thread.current_message.hwnd);
    AfxTraceOutput("m_msgCur.message = 0x%04x\n", thread.current_message.message);
    AfxTraceOutput("m_msgCur.wParam = %p\n",
        reinterpret_cast<void*>(thread.current_message.wParam));
    AfxTraceOutput("m_msgCur.lParam = %p\n",
        reinterpret_cast<void*>(thread.current_message.lParam));
}

bool IsLeftButtonUpMessage(const MSG& msg) {
    return msg.message == WM_LBUTTONUP;
}

MfcRuntimeClassCompat* GetCWndRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CWnd", static_cast<int>(sizeof(MfcCWndCompat)), 0xffff,
        +[]() -> void* {
            auto* window = new MfcCWndCompat();
            ConstructCWnd(*window);
            return window;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

void InitializeDragListMessageThunk() {
    RegisterDragListMessageGlobal();
}

UINT RegisterDragListMessageGlobal() {
    if (g_drag_list_message == 0) {
        g_drag_list_message = RegisterWindowMessageA("commctrl_DragListMsg");
    }
    return g_drag_list_message;
}

void InitializeCWndWndTop() {
    ConstructCWndWndTop();
    RegisterCWndWndTopCleanup();
}

void ConstructCWndWndTop() {
    ConstructCWndFromHandle(g_cwnd_wnd_top, HWND_TOP);
}

void RegisterCWndWndTopCleanup() {
    CrtAtexit(CleanupCWndWndTop);
}

void CleanupCWndWndTop() {
    if ((g_cwnd_static_cleanup_flags & 1U) == 0) {
        g_cwnd_static_cleanup_flags |= 1U;
        DestroyCWndCompat(g_cwnd_wnd_top);
    }
}

void InitializeCWndWndBottom() {
    ConstructCWndWndBottom();
    RegisterCWndWndBottomCleanup();
}

void ConstructCWndWndBottom() {
    ConstructCWndFromHandle(g_cwnd_wnd_bottom, HWND_BOTTOM);
}

void RegisterCWndWndBottomCleanup() {
    CrtAtexit(CleanupCWndWndBottom);
}

void CleanupCWndWndBottom() {
    if ((g_cwnd_static_cleanup_flags & 2U) == 0) {
        g_cwnd_static_cleanup_flags |= 2U;
        DestroyCWndCompat(g_cwnd_wnd_bottom);
    }
}

void InitializeCWndWndTopMost() {
    ConstructCWndWndTopMost();
    RegisterCWndWndTopMostCleanup();
}

void ConstructCWndWndTopMost() {
    ConstructCWndFromHandle(g_cwnd_wnd_top_most, HWND_TOPMOST);
}

void RegisterCWndWndTopMostCleanup() {
    CrtAtexit(CleanupCWndWndTopMost);
}

void CleanupCWndWndTopMost() {
    if ((g_cwnd_static_cleanup_flags & 4U) == 0) {
        g_cwnd_static_cleanup_flags |= 4U;
        DestroyCWndCompat(g_cwnd_wnd_top_most);
    }
}

void InitializeCWndWndNoTopMost() {
    ConstructCWndWndNoTopMost();
    RegisterCWndWndNoTopMostCleanup();
}

void ConstructCWndWndNoTopMost() {
    ConstructCWndFromHandle(g_cwnd_wnd_no_top_most, HWND_NOTOPMOST);
}

void RegisterCWndWndNoTopMostCleanup() {
    CrtAtexit(CleanupCWndWndNoTopMost);
}

void CleanupCWndWndNoTopMost() {
    if ((g_cwnd_static_cleanup_flags & 8U) == 0) {
        g_cwnd_static_cleanup_flags |= 8U;
        DestroyCWndCompat(g_cwnd_wnd_no_top_most);
    }
}

MfcCWndCompat& ConstructCWnd(MfcCWndCompat& window) {
    window.runtime_class = GetCWndRuntimeClass();
    window.window = nullptr;
    window.original_wnd_proc = nullptr;
    window.control_site = nullptr;
    window.control_container = nullptr;
    window.owner_thread = AfxGetThreadCompat();
    window.dialog_control_id = 0;
    window.wnd_flags = 0;
    window.modal_flags = 0;
    window.modal_result = 0;
    window.temporary = false;
    return window;
}

MfcCWndCompat& ConstructCWndFromHandle(MfcCWndCompat& window, HWND handle) {
    ConstructCWnd(window);
    window.window = handle;
    return window;
}

void DestroyCWndCompat(MfcCWndCompat& window) {
    if (g_window_handle_map_created && window.window != nullptr) {
        g_window_handle_map.permanent.erase(window.window);
        g_window_handle_map.temporary.erase(window.window);
    }
    window.window = nullptr;
    window.original_wnd_proc = nullptr;
    window.control_site = nullptr;
    window.control_container = nullptr;
    window.owner_thread = nullptr;
    window.dialog_control_id = 0;
    window.wnd_flags = 0;
    window.modal_flags = 0;
    window.modal_result = 0;
    window.temporary = false;
}

bool ModifyWindowLongStyle(HWND window, int index, DWORD remove_bits,
    DWORD add_bits, UINT flags) {
    if (window == nullptr) {
        return false;
    }
    const DWORD old_style = static_cast<DWORD>(GetWindowLongA(window, index));
    const DWORD new_style = (old_style & ~remove_bits) | add_bits;
    if (old_style == new_style) {
        return false;
    }
    SetWindowLongA(window, index, static_cast<LONG>(new_style));
    if (flags != 0) {
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
            flags | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void CaptureInitDialogState(MfcCWndCompat& window, RECT& rect, DWORD& style) {
    rect = RECT{};
    style = 0;
    if (window.window == nullptr) {
        return;
    }
    GetWindowRect(window.window, &rect);
    style = static_cast<DWORD>(GetWindowLongA(window.window, GWL_STYLE));
}

void ApplyInitDialogState(MfcCWndCompat& window, const RECT& old_rect,
    DWORD old_style) {
    if (window.window == nullptr || (old_style & WS_VISIBLE) != 0) {
        return;
    }

    RECT new_rect{};
    GetWindowRect(window.window, &new_rect);
    const bool same_rect = old_rect.left == new_rect.left &&
        old_rect.top == new_rect.top && old_rect.right == new_rect.right &&
        old_rect.bottom == new_rect.bottom;
    const DWORD new_style = static_cast<DWORD>(
        GetWindowLongA(window.window, GWL_STYLE));
    if (same_rect && (new_style & WS_VISIBLE) != 0) {
        ShowWindow(window.window, SW_HIDE);
    }
}

void NotifyTopLevelActivation(MfcCWndCompat& window, WPARAM state,
    MfcCWndCompat* active_window) {
    if (window.window == nullptr || active_window == nullptr ||
        active_window->window == nullptr) {
        return;
    }
    HWND top = GetAncestor(window.window, GA_ROOT);
    HWND active_top = GetAncestor(active_window->window, GA_ROOT);
    if (top != nullptr && active_top != nullptr && top != active_top) {
        SendMessageA(top, kMfcActivateTopLevelMessage, state,
            reinterpret_cast<LPARAM>(&active_top));
    }
}

LRESULT AfxCallWndProc(MfcCWndCompat& window, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    MSG previous{};
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr) {
        previous = thread->current_message;
        thread->current_message.hwnd = hwnd;
        thread->current_message.message = message;
        thread->current_message.wParam = wparam;
        thread->current_message.lParam = lparam;
    }

    LRESULT result = 0;
    bool handled = false;
    if (ObjectIsKindOfRuntimeClass(&window, GetMiniFrameRuntimeClass())) {
        auto& mini_frame = static_cast<MfcMiniFrameWndCompat&>(window);
        if (message == kMfcFloatingFrameMessage) {
            result = MiniFrameModifyStyleFlags(mini_frame,
                static_cast<DWORD>(wparam));
            handled = true;
        } else if (message == WM_NCACTIVATE) {
            result = MiniFrameOnNcActivate(mini_frame,
                static_cast<BOOL>(wparam));
            handled = true;
        }
    }

    if (!handled) {
        if (window.original_wnd_proc != nullptr) {
            result = CallWindowProcA(window.original_wnd_proc, hwnd, message,
                wparam, lparam);
        } else {
            result = DefWindowProcA(hwnd, message, wparam, lparam);
        }
    }

    if (thread != nullptr) {
        thread->current_message = previous;
    }
    return AfxCallWndProcEpilogue(result);
}

LRESULT AfxCallWndProcEpilogue(LRESULT result) {
    return result;
}

LRESULT CWndDefaultCompat(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return 0;
    }

    MSG message{};
    if (MfcWinThreadCompat* thread = AfxGetThreadCompat()) {
        message = thread->current_message;
    }
    if (message.hwnd == nullptr) {
        message.hwnd = window.window;
    }
    return DefWindowProcA(message.hwnd, message.message, message.wParam,
        message.lParam);
}

#define DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(Name) \
    LRESULT Name(MfcCWndCompat& window) { return CWndDefaultCompat(window); }
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e000e)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0021)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0049)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e005e)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0088)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e009d)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e00b2)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e00c7)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e00dc)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e00f1)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0106)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0130)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0145)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e015a)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e016f)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0184)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0199)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e01ae)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e01c3)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e01d8)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e01eb)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0200)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0215)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e022a)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0254)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0269)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e027e)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0293)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e02a8)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e02bb)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e02d0)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e02e5)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e02f8)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e030d)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0322)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0337)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0361)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0376)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e038b)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e03a0)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e03b5)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e03df)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e03f4)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0409)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e041e)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0433)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0448)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e045d)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0472)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0487)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e049c)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e04b1)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e04c6)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e04d9)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e04ec)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0501)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0516)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0529)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e053e)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0553)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0568)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e057b)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0590)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e05a5)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e05ba)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e05cf)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e05e4)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e05f9)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e060e)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0623)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndAfxWin2DefaultHandler_005e0638)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfe76)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfe8b)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfea0)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfeb3)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfec6)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfed9)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfeee)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dff18)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dff2d)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dff6c)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dff81)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dff96)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dffab)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dffc0)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dffd5)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dffe8)
DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER(CWndDefaultHandler_005dfffb)
#undef DEFINE_CWND_AFXWIN2_DEFAULT_HANDLER

int OnSetCursor(MfcCWndCompat& window, MfcCWndCompat*, UINT, UINT) {
    return static_cast<int>(CWndDefaultCompat(window));
}

void OnSize(MfcCWndCompat& window, UINT, int, int) {
    CWndDefaultCompat(window);
}

int OnNcCreate(MfcCWndCompat& window, CREATESTRUCTA*) {
    return static_cast<int>(CWndDefaultCompat(window));
}

void OnSysCommand(MfcCWndCompat& window, UINT, LPARAM) {
    CWndDefaultCompat(window);
}

void OnLButtonDblClk(MfcCWndCompat& window, UINT, POINT) {
    CWndDefaultCompat(window);
}

int OnMouseActivate(MfcCWndCompat& window, MfcCWndCompat*, UINT, UINT) {
    return static_cast<int>(CWndDefaultCompat(window));
}

int InModalState(const MfcFrameWndCompat& frame) {
    return frame.modal_disable_count != 0 ? 1 : 0;
}

void SetState(MfcButtonCompat& button, int state) {
    if (button.window != nullptr) {
        SendMessageA(button.window, BM_SETSTATE, static_cast<WPARAM>(state), 0);
    }
}

MfcWindowHandleMapCompat* GetWindowHandleMap(bool create) {
    if (!g_window_handle_map_created && !create) {
        return nullptr;
    }
    g_window_handle_map_created = true;
    return &g_window_handle_map;
}

MfcCWndCompat* CWndFromHandle(HWND handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    MfcWindowHandleMapCompat* map = GetWindowHandleMap(true);
    auto permanent = map->permanent.find(handle);
    if (permanent != map->permanent.end()) {
        return permanent->second;
    }
    auto temporary = map->temporary.find(handle);
    if (temporary != map->temporary.end()) {
        return temporary->second;
    }

    auto window = std::make_unique<MfcCWndCompat>();
    ConstructCWndFromHandle(*window, handle);
    window->temporary = true;
    MfcCWndCompat* raw = window.get();
    g_temporary_windows.push_back(std::move(window));
    map->temporary[handle] = raw;
    return raw;
}

MfcCWndCompat* CWndFromHandlePermanent(HWND handle) {
    MfcWindowHandleMapCompat* map = GetWindowHandleMap(false);
    if (map == nullptr || handle == nullptr) {
        return nullptr;
    }
    auto it = map->permanent.find(handle);
    return it == map->permanent.end() ? nullptr : it->second;
}

bool AttachCWndHandle(MfcCWndCompat& window, HWND handle) {
    if (window.window != nullptr || handle == nullptr ||
        CWndFromHandlePermanent(handle) != nullptr) {
        return false;
    }
    MfcWindowHandleMapCompat* map = GetWindowHandleMap(true);
    window.window = handle;
    window.runtime_class = GetCWndRuntimeClass();
    window.temporary = false;
    map->temporary.erase(handle);
    map->permanent[handle] = &window;
    return true;
}

HWND DetachCWndHandle(MfcCWndCompat& window) {
    HWND handle = window.window;
    if (handle == nullptr) {
        return nullptr;
    }
    MfcWindowHandleMapCompat* map = GetWindowHandleMap(false);
    if (map != nullptr) {
        auto permanent = map->permanent.find(handle);
        if (permanent != map->permanent.end() && permanent->second == &window) {
            map->permanent.erase(permanent);
        }
        map->temporary.erase(handle);
    }
    window.window = nullptr;
    window.original_wnd_proc = nullptr;
    window.temporary = false;
    return handle;
}

HWND Detach(MfcCWndCompat& window) {
    return DetachCWndHandle(window);
}

void CWndPreSubclassWindowDefault(MfcCWndCompat& window) {
    (void)window;
}

LRESULT CALLBACK AfxWndProc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    if (message == kMfcKickIdleMessage) {
        return 1;
    }
    MfcCWndCompat* window = CWndFromHandlePermanent(hwnd);
    if (window == nullptr) {
        window = CWndFromHandle(hwnd);
    }
    if (window == nullptr) {
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }
    return AfxCallWndProc(*window, hwnd, message, wparam, lparam);
}

WNDPROC GetAfxWndProc() {
    return AfxWndProc;
}

LRESULT CALLBACK AfxWndProcBase(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    auto old_proc = reinterpret_cast<WNDPROC>(
        GetPropA(hwnd, "AfxOldWndProc423"));
    if (old_proc == nullptr) {
        old_proc = DefWindowProcA;
    }

    LRESULT result = 0;
    bool call_old_proc = true;
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(old_proc));
        RemovePropA(hwnd, "AfxOldWndProc423");
        ATOM atom = GlobalFindAtomA("AfxOldWndProc423");
        if (atom != 0) {
            GlobalDeleteAtom(atom);
        }
    } else if (message == WM_ACTIVATE) {
        MfcCWndCompat* window = CWndFromHandle(hwnd);
        MfcCWndCompat* active = CWndFromHandle(reinterpret_cast<HWND>(lparam));
        if (window != nullptr) {
            NotifyTopLevelActivation(*window, wparam, active);
        }
    } else if (message == WM_SETCURSOR) {
        call_old_proc = true;
    } else if (message == WM_INITDIALOG) {
        MfcCWndCompat* window = CWndFromHandle(hwnd);
        RECT old_rect{};
        DWORD old_style = 0;
        if (window != nullptr) {
            CaptureInitDialogState(*window, old_rect, old_style);
        }
        result = CallWindowProcA(old_proc, hwnd, message, wparam, lparam);
        if (window != nullptr) {
            ApplyInitDialogState(*window, old_rect, old_style);
        }
        call_old_proc = false;
    }

    if (call_old_proc) {
        result = CallWindowProcA(old_proc, hwnd, message, wparam, lparam);
    }
    return AfxWndProcBaseEpilogue(result);
}

LRESULT AfxWndProcBaseEpilogue(LRESULT result) {
    return result;
}

LRESULT CALLBACK AfxWndProcWithControlSite(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return AfxWndProcBase(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK AfxCbtFilterHook(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HCBT_CREATEWND) {
        return CallNextHookEx(g_cbt_hook, code, wparam, lparam);
    }

    HWND hwnd = reinterpret_cast<HWND>(wparam);
    if (hwnd != nullptr && g_pending_create_window != nullptr) {
        AttachCWndHandle(*g_pending_create_window, hwnd);
        WNDPROC old_proc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
        if (old_proc != nullptr && GetPropA(hwnd, "AfxOldWndProc423") == nullptr) {
            SetPropA(hwnd, "AfxOldWndProc423",
                reinterpret_cast<HANDLE>(old_proc));
            GlobalAddAtomA("AfxOldWndProc423");
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(GetAfxWndProc()));
            g_pending_create_window->original_wnd_proc = old_proc;
        }
        g_pending_create_window = nullptr;
    }
    return CallNextHookEx(g_cbt_hook, code, wparam, lparam);
}

void AfxHookWindowCreate(MfcCWndCompat& window) {
    if (g_pending_create_window == &window) {
        return;
    }
    if (g_cbt_hook == nullptr) {
        g_cbt_hook = SetWindowsHookExA(WH_CBT, AfxCbtFilterHook, nullptr,
            GetCurrentThreadId());
        if (g_cbt_hook == nullptr) {
            ThrowMfcMemoryException();
        }
    }
    g_pending_create_window = &window;
}

bool AfxUnhookWindowCreate() {
    const bool had_no_pending_window = g_pending_create_window == nullptr;
    g_pending_create_window = nullptr;
    if (g_cbt_hook != nullptr) {
        UnhookWindowsHookEx(g_cbt_hook);
        g_cbt_hook = nullptr;
    }
    return had_no_pending_window;
}

bool CreateWindowExFromRect(MfcCWndCompat& window, DWORD ex_style,
    const char* class_name, const char* window_name, DWORD style,
    const RECT& rect, HWND parent, HMENU menu, void* param) {
    return CreateWindowExCompat(window, ex_style, class_name, window_name,
        style, rect.left, rect.top, rect.right - rect.left,
        rect.bottom - rect.top, parent, menu, param);
}

bool CreateWindowExCompat(MfcCWndCompat& window, DWORD ex_style,
    const char* class_name, const char* window_name, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, void* param) {
    if ((class_name == nullptr || class_name[0] == '\0') &&
        !EnsureAfxWindowClass(window)) {
        return false;
    }

    const char* effective_class = class_name;
    if (effective_class == nullptr || effective_class[0] == '\0') {
        effective_class = window.class_name.c_str();
    }

    AfxHookWindowCreate(window);
    HWND hwnd = CreateWindowExA(ex_style, effective_class, window_name, style,
        x, y, width, height, parent, menu, GetModuleHandleA(nullptr), param);
    const bool unhooked_without_pending = AfxUnhookWindowCreate();
    if (hwnd == nullptr) {
        AfxTraceOutput("Warning: Window creation failed: %lu\n", GetLastError());
        return false;
    }
    if (window.window == nullptr || !unhooked_without_pending) {
        AttachCWndHandle(window, hwnd);
    }
    return window.window == hwnd;
}

bool EnsureAfxWindowClass(MfcCWndCompat& window) {
    if (!window.class_name.empty()) {
        return true;
    }

    const char* class_name = "AfxWnd42sd";
    WNDCLASSA wc{};
    if (GetClassInfoA(GetModuleHandleA(nullptr), class_name, &wc) == 0) {
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        if (RegisterClassA(&wc) == 0) {
            return false;
        }
    }
    window.class_name = class_name;
    return true;
}

bool CreateAfxRegisteredWindow(MfcCWndCompat& window, const char* class_name,
    const char* window_name, DWORD style, const RECT& rect, HWND parent,
    HMENU menu, void* param) {
    if (parent == nullptr || (style & WS_POPUP) != 0) {
        return false;
    }
    return CreateWindowExFromRect(window, 0, class_name, window_name,
        style | WS_CHILD, rect, parent, menu, param);
}

void CWndDefaultAndReleaseControlSite(MfcCWndCompat& window) {
    window.control_site = nullptr;
    if (window.window != nullptr) {
        DefWindowProcA(window.window, WM_NULL, 0, 0);
    }
}

void CWndOnNcDestroy(MfcCWndCompat& window) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr) {
        if (thread->main_window == window.window) {
            PostQuitMessage(0);
            thread->main_window = nullptr;
        }
        if (thread->active_window == window.window) {
            thread->active_window = nullptr;
        }
    }

    WNDPROC old_proc = window.original_wnd_proc;
    if (window.window != nullptr && old_proc != nullptr) {
        const LONG_PTR current = GetWindowLongPtrA(window.window, GWLP_WNDPROC);
        DefWindowProcA(window.window, WM_NCDESTROY, 0, 0);
        if (GetWindowLongPtrA(window.window, GWLP_WNDPROC) == current) {
            SetWindowLongPtrA(window.window, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(old_proc));
        }
    }
    DestroyCWndCompat(window);
    CWndPostNcDestroyDefault(window);
}

void CWndPostNcDestroyDefault(MfcCWndCompat& window) {
    (void)window;
}

void CWndAssertValid(const MfcCWndCompat& window) {
    if (window.window == nullptr || window.window == HWND_BOTTOM ||
        window.window == HWND_TOPMOST || window.window == HWND_NOTOPMOST) {
        return;
    }
    if (!IsWindow(window.window)) {
        CrtDbgReport(2, "wincore.cpp", 0x36b, nullptr,
            "invalid CWnd HWND");
    }
}

void CWndDump(const MfcCWndCompat& window) {
    AfxTraceOutput("m_hWnd = %p\n", window.window);
    if (window.window == nullptr || window.window == HWND_BOTTOM ||
        window.window == HWND_TOPMOST || window.window == HWND_NOTOPMOST) {
        return;
    }
    if (!IsWindow(window.window)) {
        AfxTraceOutput("illegal HWND\n");
        return;
    }

    char text[64]{};
    GetWindowTextA(window.window, text, static_cast<int>(sizeof(text)));
    char class_name[64]{};
    GetClassNameA(window.window, class_name, static_cast<int>(sizeof(class_name)));
    RECT rect{};
    GetWindowRect(window.window, &rect);
    AfxTraceOutput("caption = %s\n", text);
    AfxTraceOutput("class name = %s\n", class_name);
    AfxTraceOutput("rect = (%ld,%ld)-(%ld,%ld)\n", rect.left, rect.top,
        rect.right, rect.bottom);
    AfxTraceOutput("style = 0x%08lx\n", GetWindowLongA(window.window, GWL_STYLE));
}

bool CWndDestroyWindow(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return false;
    }
    HWND handle = window.window;
    const BOOL destroyed = DestroyWindow(handle);
    if (destroyed != 0 && window.window == handle) {
        DestroyCWndCompat(window);
    }
    return destroyed != 0;
}

WNDPROC* CWndGetSuperWndProcAddr(MfcCWndCompat& window) {
    return &window.original_wnd_proc;
}

void CWndOnCancelMode(MfcCWndCompat& window) {
    if (window.window != nullptr) {
        SendMessageA(window.window, WM_CANCELMODE, 0, 0);
    }
}

UINT CWndOnToolHitTest(MfcCWndCompat& window, POINT point, TOOLINFOA* tool_info) {
    if (window.window == nullptr) {
        return static_cast<UINT>(-1);
    }
    HWND child = ChildWindowFromPoint(window.window, point);
    if (child == nullptr) {
        return static_cast<UINT>(-1);
    }
    const UINT id = static_cast<UINT>(GetDlgCtrlID(child)) & 0xffffU;
    if (tool_info != nullptr && tool_info->cbSize >= sizeof(TOOLINFOA)) {
        tool_info->hwnd = window.window;
        tool_info->uId = reinterpret_cast<UINT_PTR>(child);
        tool_info->uFlags |= TTF_IDISHWND;
        tool_info->lParam = 0;
    }
    return id;
}

std::string CWndGetWindowTextCompat(const MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return {};
    }
    const int length = GetWindowTextLengthA(window.window);
    std::string text(static_cast<std::size_t>(length) + 1U, '\0');
    GetWindowTextA(window.window, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::string CWndGetDlgItemTextCompat(const MfcCWndCompat& window, int control_id) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return {};
    }
    HWND child = GetDlgItem(window.window, control_id);
    if (child == nullptr) {
        return {};
    }
    const int length = GetWindowTextLengthA(child);
    std::string text(static_cast<std::size_t>(length) + 1U, '\0');
    GetWindowTextA(child, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

bool CWndGetWindowPlacementCompat(const MfcCWndCompat& window,
    WINDOWPLACEMENT& placement) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    placement.length = sizeof(WINDOWPLACEMENT);
    return GetWindowPlacement(window.window, &placement) != 0;
}

bool CWndSetWindowPlacementCompat(const MfcCWndCompat& window,
    WINDOWPLACEMENT& placement) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    placement.length = sizeof(WINDOWPLACEMENT);
    return SetWindowPlacement(window.window, &placement) != 0;
}

void CWndOnDrawItem(MfcCWndCompat& window, int control_id,
    DRAWITEMSTRUCT* draw_item) {
    (void)control_id;
    if (draw_item == nullptr || window.window == nullptr) {
        return;
    }
    DefWindowProcA(window.window, WM_DRAWITEM,
        static_cast<WPARAM>(draw_item->CtlID),
        reinterpret_cast<LPARAM>(draw_item));
}

BOOL CWndTrackPopupMenuCompat(HMENU menu, UINT flags, int x, int y,
    HWND owner, const RECT* rect) {
    if (menu == nullptr) {
        return FALSE;
    }
    return TrackPopupMenu(menu, flags, x, y, 0, owner, rect);
}

void CWndOnMeasureItem(MfcCWndCompat& window, int control_id,
    MEASUREITEMSTRUCT* measure_item) {
    (void)control_id;
    if (measure_item == nullptr || window.window == nullptr) {
        return;
    }
    DefWindowProcA(window.window, WM_MEASUREITEM,
        static_cast<WPARAM>(measure_item->CtlID),
        reinterpret_cast<LPARAM>(measure_item));
}

bool AfxRegisterClassCompat(WNDCLASSA& window_class) {
    WNDCLASSA existing{};
    if (GetClassInfoA(window_class.hInstance, window_class.lpszClassName,
        &existing) != 0) {
        return true;
    }
    if (RegisterClassA(&window_class) == 0) {
        AfxTraceOutput("Can't register window class named %s\n",
            window_class.lpszClassName);
        return false;
    }
    return AfxRegisterClassEpilogue();
}

bool AfxRegisterClassEpilogue() {
    return true;
}

const char* AfxRegisterWndClassCompat(UINT class_style, HCURSOR cursor,
    HBRUSH brush, HICON icon) {
    char name[128]{};
    HINSTANCE instance = GetModuleHandleA(nullptr);
    if (cursor == nullptr && brush == nullptr && icon == nullptr) {
        std::snprintf(name, sizeof(name), "Afx:%p:%x", instance, class_style);
    } else {
        std::snprintf(name, sizeof(name), "Afx:%p:%x:%p:%p:%p", instance,
            class_style, cursor, brush, icon);
    }

    WNDCLASSA wc{};
    if (GetClassInfoA(instance, name, &wc) == 0) {
        wc.style = class_style;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = instance;
        wc.hIcon = icon;
        wc.hCursor = cursor;
        wc.hbrBackground = brush;
        wc.lpszClassName = name;
        if (!AfxRegisterClassCompat(wc)) {
            ThrowMfcResourceException();
        }
    }
    g_registered_window_class_name = name;
    return g_registered_window_class_name.c_str();
}

void CWndOnCtlColorCompat(MfcCWndCompat& window, HDC dc, HWND child) {
    if (window.window != nullptr) {
        DefWindowProcA(window.window, WM_CTLCOLORSTATIC,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(child));
    }
}

void CWndWinHelp(MfcCWndCompat& window, ULONG_PTR data, UINT command,
    const char* help_file) {
    if (window.window == nullptr || help_file == nullptr || help_file[0] == '\0') {
        return;
    }
    HWND capture = GetCapture();
    if (capture != nullptr) {
        SendMessageA(capture, WM_CANCELMODE, 0, 0);
    }
    if (WinHelpA(window.window, help_file, command, data) == 0) {
        AfxTraceOutput("WinHelp failed for %s command=%u data=%p\n",
            help_file, command, reinterpret_cast<void*>(data));
    }
}

const MfcMessageMapEntryCompat* GetCWndMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NULL, 0, 0, 0, nullptr, 0},
    };
    return entries;
}

const MfcMessageMapEntryCompat* FindMessageMapEntry(
    const MfcMessageMapEntryCompat* entries, UINT message, UINT code, UINT id) {
    if (entries == nullptr) {
        return nullptr;
    }
    for (const MfcMessageMapEntryCompat* entry = entries;
         entry->message != WM_NULL || entry->handler != nullptr; ++entry) {
        if (entry->message == message && entry->code == code &&
            entry->id_first <= id && id <= entry->id_last) {
            return entry;
        }
    }
    return nullptr;
}

void ResetMessageMapCache() {
}

bool CWndOnWndMsg(MfcCWndCompat& window, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT* result) {
    LRESULT local_result = 0;
    LRESULT* out = result == nullptr ? &local_result : result;

    if (message == WM_COMMAND) {
        const UINT id = LOWORD(wparam);
        const UINT code = HIWORD(wparam);
        if (ObjectIsKindOfRuntimeClass(&window, GetFrameWndRuntimeClass()) &&
            FrameWndOnCmdMsg(static_cast<MfcFrameWndCompat&>(window), id,
                static_cast<int>(code), nullptr, nullptr)) {
            *out = 1;
            return true;
        }
        if (FindMessageMapEntry(GetCWndMessageMap(), message, code, id) != nullptr) {
            *out = 1;
            return true;
        }
        return false;
    }
    if (message == WM_NOTIFY) {
        auto* notify = reinterpret_cast<NMHDR*>(lparam);
        if (notify != nullptr &&
            FindMessageMapEntry(GetCWndMessageMap(), message, notify->code,
                static_cast<UINT>(wparam)) != nullptr) {
            *out = 1;
            return true;
        }
        return false;
    }
    if (message == WM_ACTIVATE) {
        NotifyTopLevelActivation(window, wparam,
            CWndFromHandle(reinterpret_cast<HWND>(lparam)));
    }
    if (message == WM_SETCURSOR) {
        *out = TRUE;
        return true;
    }

    const MfcMessageMapEntryCompat* entry =
        FindMessageMapEntry(GetCWndMessageMap(), message, 0, 0);
    if (entry == nullptr || entry->handler == nullptr) {
        return false;
    }
    *out = 1;
    return true;
}

void TestCmdUISetCheckNoop() {
}

void TestCmdUISetRadioNoop() {
}

void TestCmdUISetTextNoop() {
}

bool CWndOnCommand(MfcCWndCompat& window, WPARAM wparam, HWND control) {
    const UINT id = LOWORD(wparam);
    const UINT code = HIWORD(wparam);
    if (control != nullptr) {
        MfcCWndCompat* child = CWndFromHandlePermanent(control);
        if (child != nullptr) {
            LRESULT child_result = 0;
            if (CWndOnWndMsg(*child, WM_COMMAND, wparam, 0, &child_result)) {
                return true;
            }
        }
    }
    if (id == 0) {
        return false;
    }
    LRESULT result = 0;
    return CWndOnWndMsg(window, WM_COMMAND, MAKEWPARAM(id, code),
        reinterpret_cast<LPARAM>(control), &result);
}

bool CWndOnNotify(MfcCWndCompat& window, WPARAM control_id, NMHDR* notify,
    LRESULT* result) {
    if (notify == nullptr || notify->hwndFrom == nullptr) {
        return false;
    }
    MfcCWndCompat* child = CWndFromHandlePermanent(notify->hwndFrom);
    if (child != nullptr && CWndOnWndMsg(*child, WM_NOTIFY, control_id,
        reinterpret_cast<LPARAM>(notify), result)) {
        return true;
    }
    return CWndOnWndMsg(window, WM_NOTIFY, control_id,
        reinterpret_cast<LPARAM>(notify), result);
}

HWND GetWindowOwnerOrParent(HWND window) {
    MfcCWndCompat* permanent = CWndFromHandlePermanent(window);
    if (permanent != nullptr) {
        HWND owner = GetWindow(permanent->window, GW_OWNER);
        return owner != nullptr ? owner : GetParent(permanent->window);
    }
    const DWORD style = static_cast<DWORD>(GetWindowLongA(window, GWL_STYLE));
    if ((style & WS_CHILD) == 0) {
        return GetWindow(window, GW_OWNER);
    }
    return GetParent(window);
}

MfcCWndCompat* CWndGetTopLevelWindow(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND current = window.window;
    for (;;) {
        HWND next = GetWindowOwnerOrParent(current);
        if (next == nullptr) {
            break;
        }
        current = next;
    }
    return CWndFromHandle(current);
}

bool CWndIsTopParentActive(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return false;
    }
    HWND foreground = GetForegroundWindow();
    MfcCWndCompat* top = GetTopLevelParent(window);
    if (top != nullptr && top->window != nullptr) {
        HWND popup = GetLastActivePopup(top->window);
        if (popup != nullptr) {
            top = CWndFromHandle(popup);
        }
    }
    if (foreground == nullptr || top == nullptr) {
        return false;
    }
    return foreground == top->window;
}

void AfxDoForAllClasses(void (*callback)(const MfcRuntimeClassCompat*, void*),
    void* context) {
    if (callback == nullptr) {
        return;
    }
    for (MfcRuntimeClassCompat* runtime_class = GetFirstRuntimeClass();
         runtime_class != nullptr; runtime_class = runtime_class->next_class) {
        callback(runtime_class, context);
    }
}

int AfxHandleSetCursor(MfcCWndCompat* window, UINT hit_test, UINT message) {
    if (window == nullptr || hit_test != static_cast<UINT>(HTERROR)) {
        return 0;
    }
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONDBLCLK &&
        message != WM_RBUTTONDOWN) {
        return 0;
    }

    MfcCWndCompat* top = GetTopLevelParent(*window);
    HWND popup = top == nullptr || top->window == nullptr
        ? nullptr : GetLastActivePopup(top->window);
    HWND foreground = GetForegroundWindow();
    if (popup != nullptr && popup != foreground && IsWindowEnabled(popup)) {
        SetForegroundWindow(popup);
        return 1;
    }
    return 0;
}

MfcMenuCompat* AfxFindPopupMenuFromID(MfcMenuCompat* menu, UINT command_id) {
    if (menu == nullptr || menu->menu == nullptr) {
        return nullptr;
    }
    const int count = MenuGetItemCount(*menu);
    for (int index = 0; index < count; ++index) {
        MfcMenuCompat* submenu = MenuGetSubMenu(*menu, index);
        if (submenu == nullptr) {
            if (MenuGetItemID(*menu, index) == command_id) {
                return CMenuFromHandlePermanent(menu->menu);
            }
            continue;
        }
        MfcMenuCompat* found = AfxFindPopupMenuFromID(submenu, command_id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

MfcCWndCompat* GetTopLevelParent(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND current = window.window;
    for (;;) {
        HWND next = GetWindowOwnerOrParent(current);
        if (next == nullptr) {
            break;
        }
        current = next;
    }
    return CWndFromHandle(current);
}

MfcCWndCompat* GetTopLevelOwner(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND current = window.window;
    for (;;) {
        HWND owner = GetWindow(current, GW_OWNER);
        if (owner == nullptr) {
            break;
        }
        current = owner;
    }
    return CWndFromHandle(current);
}

void ActivateTopParent(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return;
    }
    MfcCWndCompat* top = GetTopLevelParent(window);
    HWND foreground = GetForegroundWindow();
    if (foreground == window.window ||
        (foreground != nullptr && IsChild(foreground, window.window) != 0)) {
        return;
    }
    if (top != nullptr && top->window != nullptr) {
        SetForegroundWindow(top->window);
    }
}

MfcFrameWndCompat* CWndGetParentFrameCompat(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    for (HWND parent = GetParent(window.window); parent != nullptr;
         parent = GetParent(parent)) {
        MfcCWndCompat* candidate = CWndFromHandlePermanent(parent);
        if (candidate != nullptr &&
            ObjectIsKindOfRuntimeClass(candidate, GetFrameWndRuntimeClass())) {
            return static_cast<MfcFrameWndCompat*>(candidate);
        }
    }
    return nullptr;
}

MfcFrameWndCompat* GetTopLevelFrame(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    MfcFrameWndCompat* frame = nullptr;
    if (ObjectIsKindOfRuntimeClass(&window, GetFrameWndRuntimeClass())) {
        frame = static_cast<MfcFrameWndCompat*>(&window);
    } else {
        frame = CWndGetParentFrameCompat(window);
    }
    while (frame != nullptr) {
        MfcFrameWndCompat* parent = CWndGetParentFrameCompat(*frame);
        if (parent == nullptr) {
            return frame;
        }
        frame = parent;
    }
    return nullptr;
}

int CWndSetScrollPosCompat(MfcCWndCompat& window, int bar, int position,
    BOOL redraw) {
    if (window.window == nullptr) {
        return 0;
    }
    return SetScrollPos(window.window, bar, position, redraw);
}

void CWndGetScrollRangeCompat(MfcCWndCompat& window, int bar,
    int* min_position, int* max_position) {
    if (window.window == nullptr) {
        if (min_position != nullptr) {
            *min_position = 0;
        }
        if (max_position != nullptr) {
            *max_position = 0;
        }
        return;
    }
    GetScrollRange(window.window, bar, min_position, max_position);
}

void EnableScrollBarCtrl(MfcCWndCompat& window, int bar, BOOL enable) {
    if (bar == SB_BOTH) {
        EnableScrollBarCtrl(window, SB_HORZ, enable);
        EnableScrollBarCtrl(window, SB_VERT, enable);
        return;
    }
    if (window.window == nullptr) {
        return;
    }
    ShowScrollBar(window.window, bar, enable);
}

int CWndMessageBox(MfcCWndCompat& window, const char* text,
    const char* caption, UINT type) {
    if (caption == nullptr) {
        caption = "Ranker";
    }
    return MessageBoxA(window.window, text == nullptr ? "" : text, caption, type);
}

bool CWndOnHelpInfoDefault() {
    return false;
}

bool CWndSetScrollInfoCompat(MfcCWndCompat& window, int bar,
    SCROLLINFO& info, BOOL redraw) {
    if (window.window == nullptr) {
        return false;
    }
    info.cbSize = sizeof(SCROLLINFO);
    return SetScrollInfo(window.window, bar, &info, redraw) != 0;
}

BOOL CWndGetScrollInfoCompat(MfcCWndCompat& window, int bar,
    SCROLLINFO& info, UINT mask) {
    if (window.window == nullptr) {
        return FALSE;
    }
    info.cbSize = sizeof(SCROLLINFO);
    info.fMask = mask;
    return GetScrollInfo(window.window, bar, &info);
}

int CWndGetScrollLimitCompat(MfcCWndCompat& window, int bar) {
    SCROLLINFO info{};
    if (!CWndGetScrollInfoCompat(window, bar, info, SIF_PAGE | SIF_RANGE | SIF_POS)) {
        return 0;
    }
    const int page = info.nPage <= 1 ? 0 : static_cast<int>(info.nPage) - 1;
    return info.nMax - info.nMin - page;
}

void CWndScrollWindowCompat(MfcCWndCompat& window, int dx, int dy,
    const RECT* scroll_rect, const RECT* clip_rect) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return;
    }
    if (scroll_rect == nullptr && clip_rect == nullptr) {
        for (HWND child = GetWindow(window.window, GW_CHILD); child != nullptr;
             child = GetNextWindow(child, GW_HWNDNEXT)) {
            RECT child_rect{};
            GetWindowRect(child, &child_rect);
            MapWindowPoints(nullptr, window.window,
                reinterpret_cast<POINT*>(&child_rect), 2);
            SetWindowPos(child, nullptr, child_rect.left + dx,
                child_rect.top + dy, 0, 0,
                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    } else {
        ScrollWindow(window.window, dx, dy, scroll_rect, clip_rect);
    }
}

void CWndRepositionBars(MfcCWndCompat& window, UINT id_first, UINT id_last,
    UINT id_leftover, int repos_query, RECT* rect_param, const RECT* client_rect,
    bool stretch) {
    if (window.window == nullptr) {
        return;
    }
    RECT layout{};
    if (client_rect != nullptr) {
        layout = *client_rect;
    } else {
        GetClientRect(window.window, &layout);
    }

    HDWP hdwp = repos_query == 1 ? nullptr : BeginDeferWindowPos(8);
    HWND leftover = nullptr;
    for (HWND child = GetTopWindow(window.window); child != nullptr;
         child = GetNextWindow(child, GW_HWNDNEXT)) {
        const UINT id = static_cast<UINT>(GetDlgCtrlID(child)) & 0xffffU;
        if (id == id_leftover) {
            leftover = child;
        } else if (id_first <= id && id <= id_last &&
            CWndFromHandlePermanent(child) != nullptr) {
            SendMessageA(child, kMfcActivateTopLevelMessage - 0x0d, 0,
                reinterpret_cast<LPARAM>(&hdwp));
        }
    }

    if (repos_query == 1) {
        if (rect_param != nullptr) {
            *rect_param = stretch ? layout : RECT{0, 0,
                layout.right - layout.left, layout.bottom - layout.top};
        }
        return;
    }

    if (leftover != nullptr) {
        RECT target = layout;
        if (repos_query == 2 && rect_param != nullptr) {
            target.left += rect_param->left;
            target.top += rect_param->top;
            target.right -= rect_param->right;
            target.bottom -= rect_param->bottom;
        }
        DeferMoveWindow(&hdwp, leftover, target);
    }
    if (hdwp == nullptr || EndDeferWindowPos(hdwp) == 0) {
        AfxTraceOutput("Warning: DeferWindowPos failed.\n");
    }
}

void DeferMoveWindow(HDWP* hdwp, HWND window, const RECT& rect) {
    if (window == nullptr) {
        return;
    }
    HWND parent = GetParent(window);
    if (parent == nullptr) {
        return;
    }
    RECT old_rect{};
    GetWindowRect(window, &old_rect);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&old_rect), 2);
    if (EqualRect(&old_rect, &rect)) {
        return;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (hdwp == nullptr) {
        SetWindowPos(window, nullptr, rect.left, rect.top, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (*hdwp != nullptr) {
        *hdwp = DeferWindowPos(*hdwp, window, nullptr, rect.left, rect.top,
            width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CWndCalcWindowRect(MfcCWndCompat& window, RECT& rect, bool include_menu) {
    DWORD ex_style = 0;
    DWORD style = 0;
    if (window.window != nullptr) {
        ex_style = static_cast<DWORD>(GetWindowLongA(window.window, GWL_EXSTYLE));
        style = static_cast<DWORD>(GetWindowLongA(window.window, GWL_STYLE));
    }
    AdjustWindowRectEx(&rect, style, include_menu ? TRUE : FALSE, ex_style);
}

bool CWndOnSysCommand(MfcCWndCompat& window, UINT command, LPARAM lparam) {
    MfcCWndCompat* top = CWndGetTopLevelWindow(window);
    const UINT system_command = command & 0xfff0U;
    switch (system_command) {
    case SC_NEXTWINDOW:
    case SC_PREVWINDOW:
        if (LOWORD(static_cast<DWORD_PTR>(lparam)) == VK_F6 && top != nullptr) {
            CWndSetFocus(window);
            return true;
        }
        return false;
    case SC_CLOSE:
    case SC_KEYMENU:
        if ((system_command == SC_CLOSE || lparam != 0) && top != nullptr) {
            HWND active = window.window;
            HWND focus = GetFocus();
            if (top->window != nullptr) {
                SetActiveWindow(top->window);
                if (top->window == window.window) {
                    DefWindowProcA(window.window, WM_SYSCOMMAND, command, lparam);
                } else {
                    SendMessageA(top->window, WM_SYSCOMMAND, command, lparam);
                }
            }
            if (active != nullptr && IsWindow(active)) {
                SetActiveWindow(active);
            }
            if (focus != nullptr && IsWindow(focus)) {
                SetFocus(focus);
            }
        }
        return true;
    default:
        return false;
    }
}

bool AfxPreTranslateMessageFromWindow(HWND stop, MSG& msg) {
    if (msg.hwnd != nullptr && !IsWindow(msg.hwnd)) {
        return false;
    }
    for (HWND current = msg.hwnd; current != nullptr; current = GetParent(current)) {
        MfcCWndCompat* window = CWndFromHandlePermanent(current);
        if (window != nullptr) {
            LRESULT result = 0;
            if (CWndOnWndMsg(*window, msg.message, msg.wParam, msg.lParam,
                &result)) {
                return true;
            }
        }
        if (current == stop) {
            break;
        }
    }
    return false;
}

bool CWndSendChildNotifyLastMsgByHandle(HWND window, LRESULT* result) {
    MfcCWndCompat* target = CWndFromHandlePermanent(window);
    if (target == nullptr && window != nullptr) {
        HWND parent = GetParent(window);
        MfcCWndCompat* parent_window = CWndFromHandlePermanent(parent);
        if (parent_window != nullptr) {
            target = CWndFromHandle(window);
        }
    }
    if (target == nullptr) {
        return false;
    }
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread == nullptr) {
        return false;
    }
    return CWndOnWndMsg(*target, thread->current_message.message,
        thread->current_message.wParam, thread->current_message.lParam, result);
}

bool CWndReflectChildNotify(UINT message, WPARAM wparam, LPARAM lparam,
    LRESULT* result) {
    if (message == WM_COMMAND) {
        return FindMessageMapEntry(GetCWndMessageMap(), message,
            HIWORD(wparam), LOWORD(wparam)) != nullptr;
    }
    if (message == WM_NOTIFY) {
        auto* notify = reinterpret_cast<NMHDR*>(lparam);
        return notify != nullptr && FindMessageMapEntry(GetCWndMessageMap(),
            message, notify->code, static_cast<UINT>(wparam)) != nullptr;
    }
    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        LRESULT local = 0;
        MfcCWndCompat* reflected =
            CWndFromHandle(reinterpret_cast<HWND>(lparam));
        if (reflected == nullptr) {
            return false;
        }
        bool handled = CWndOnWndMsg(*reflected, message + 0xbc00, wparam,
            lparam, &local);
        if (handled && result != nullptr) {
            *result = local;
        }
        return handled;
    }
    return false;
}

void CWndOnParentNotify(MfcCWndCompat& window, UINT event, HWND child) {
    if ((event == WM_CREATE || event == WM_DESTROY) &&
        CWndSendChildNotifyLastMsgByHandle(child, nullptr)) {
        return;
    }
    if (window.window != nullptr) {
        DefWindowProcA(window.window, WM_PARENTNOTIFY, event,
            reinterpret_cast<LPARAM>(child));
    }
}

bool CWndOnEnable(MfcCWndCompat& window, BOOL enabled) {
    if (!enabled) {
        CWndOnCancelMode(window);
    }
    return false;
}

void CWndOnShowWindow(MfcCWndCompat& window) {
    if (window.window != nullptr) {
        DefWindowProcA(window.window, WM_SHOWWINDOW, 0, 0);
    }
}

void CWndOnActivateApp(MfcCWndCompat& window) {
    ResetMessageMapCache();
    CWndOnActivate(window);
}

void CWndOnSetFocus(MfcCWndCompat& window) {
    CWndOnActivate(window);
}

void CWndOnKillFocus(MfcCWndCompat& window, HWND focus) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr) {
        thread->active_window = focus;
    }
    if (window.window != nullptr) {
        DefWindowProcA(window.window, WM_KILLFOCUS,
            reinterpret_cast<WPARAM>(focus), 0);
    }
}

LRESULT CWndOnHelpCommand(MfcCWndCompat& window) {
    if (window.window != nullptr && GetKeyState(VK_SHIFT) >= 0 &&
        GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) {
        SendMessageA(window.window, WM_COMMAND, kMfcHelpCommand, 0);
        return 1;
    }
    return window.window == nullptr ? 0 :
        DefWindowProcA(window.window, WM_HELP, 0, 0);
}

void CWndOnActivate(MfcCWndCompat& window) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->main_window == window.window) {
        thread->active_window = window.window;
    }
    if (window.window != nullptr) {
        DefWindowProcA(window.window, WM_ACTIVATE, 0, 0);
    }
}

LRESULT CWndSendChildNotifyOrDefault(MfcCWndCompat& window, HWND child) {
    LRESULT result = 0;
    if (!CWndSendChildNotifyLastMsgByHandle(child, &result) &&
        window.window != nullptr) {
        result = DefWindowProcA(window.window, WM_NULL, 0, 0);
    }
    return result;
}

LRESULT CWndForwardChildNotifyOrDefault(MfcCWndCompat& window,
    MfcCWndCompat* child) {
    LRESULT result = 0;
    if (child == nullptr ||
        !CWndOnWndMsg(*child, WM_NULL, 0, 0, &result)) {
        result = window.window == nullptr ? 0 :
            DefWindowProcA(window.window, WM_NULL, 0, 0);
    }
    return result;
}

LRESULT CWndOnCtlColor(MfcCWndCompat& window, HDC dc, HWND child, UINT type) {
    LRESULT result = 0;
    if (CWndSendChildNotifyLastMsgByHandle(child, &result)) {
        return result;
    }
    if (ApplyCtlColorBrushColors(dc,
        reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)),
        GetSysColor(COLOR_WINDOWTEXT))) {
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    return window.window == nullptr ? 0 :
        DefWindowProcA(window.window, WM_CTLCOLORMSGBOX + type,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(child));
}

bool ApplyCtlColorBrushColors(HDC dc, HBRUSH brush, COLORREF text_color) {
    if (dc == nullptr || brush == nullptr) {
        return false;
    }
    LOGBRUSH brush_info{};
    if (GetObjectA(brush, sizeof(brush_info), &brush_info) == 0) {
        return false;
    }
    SetBkColor(dc, brush_info.lbColor);
    SetTextColor(dc, text_color == static_cast<COLORREF>(-1)
        ? GetSysColor(COLOR_WINDOWTEXT) : text_color);
    return true;
}

UINT CWndGetDlgCtrlIDDefault() {
    return 0xffffU;
}

bool CWndUpdateData(MfcCWndCompat& window, bool save_and_validate) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    (void)save_and_validate;
    return CWndUpdateDataEpilogue(true);
}

bool CWndUpdateDataEpilogue(bool result) {
    return result;
}

void CWndCenterWindow(MfcCWndCompat& window, HWND alternate_owner) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return;
    }

    HWND owner = alternate_owner;
    if (owner == nullptr) {
        owner = GetWindow(window.window, GW_OWNER);
        if (owner == nullptr) {
            owner = GetParent(window.window);
        }
    }

    RECT window_rect{};
    GetWindowRect(window.window, &window_rect);
    RECT owner_rect{};
    RECT area{};
    if (owner != nullptr && IsWindow(owner)) {
        GetWindowRect(owner, &owner_rect);
        HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoA(monitor, &info) != 0) {
            area = info.rcWork;
        } else {
            SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
        }
    } else {
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
        owner_rect = area;
    }

    const int width = window_rect.right - window_rect.left;
    const int height = window_rect.bottom - window_rect.top;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    if (x < area.left) {
        x = area.left;
    }
    if (y < area.top) {
        y = area.top;
    }
    if (x + width > area.right) {
        x = area.right - width;
    }
    if (y + height > area.bottom) {
        y = area.bottom - height;
    }
    SetWindowPos(window.window, nullptr, x, y, 0, 0,
        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool CWndOnInitDialogDefault(MfcCWndCompat& window) {
    (void)window;
    return true;
}

bool ExecuteDlgInitResource(MfcCWndCompat& window, const char* resource_name) {
    if (resource_name == nullptr) {
        return ExecuteDlgInitStream(window, nullptr);
    }

    HINSTANCE instance = GetModuleHandleA(nullptr);
    HRSRC resource = FindResourceA(instance, resource_name, MAKEINTRESOURCEA(240));
    if (resource == nullptr) {
        return ExecuteDlgInitStream(window, nullptr);
    }

    HGLOBAL resource_handle = LoadResource(instance, resource);
    if (resource_handle == nullptr) {
        return false;
    }

    void* resource_data = LockResource(resource_handle);
    if (resource_data == nullptr) {
        return false;
    }

    const bool result = ExecuteDlgInitStream(window, resource_data);
    UnlockResource(resource_handle);
    FreeResource(resource_handle);
    return result;
}

bool ExecuteDlgInitStream(MfcCWndCompat& window, const void* resource_data) {
    bool result = true;
    if (resource_data != nullptr && window.window != nullptr) {
        const auto* cursor = static_cast<const unsigned char*>(resource_data);
        while (result) {
            const WORD control_id = *reinterpret_cast<const WORD*>(cursor);
            if (control_id == 0) {
                break;
            }

            WORD message = *reinterpret_cast<const WORD*>(cursor + 2);
            const DWORD length = *reinterpret_cast<const DWORD*>(cursor + 4);
            cursor += 8;

            if (message == 0x1234) {
                message = WM_USER + 1;
            } else if (message == WM_USER + 1) {
                message = LB_ADDSTRING;
            } else if (message == WM_USER + 3) {
                message = CB_ADDSTRING;
            }

            if (message == LB_ADDSTRING || message == CB_ADDSTRING ||
                message == WM_USER + 1) {
                if (length == 0 || cursor[length - 1] != '\0') {
                    result = false;
                } else {
                    LPARAM data = reinterpret_cast<LPARAM>(cursor);
                    const LRESULT sent = SendDlgItemMessageA(window.window,
                        control_id, message, 0, data);
                    result = sent != LB_ERR;
                }
            }

            cursor += length;
        }
    }

    if (result && window.window != nullptr) {
        SendMessageA(window.window, kMfcInitialUpdateMessage, 0, 0);
        send_message_to_descendants(window.window, kMfcInitialUpdateMessage, 0, 0);
    }
    return result;
}

void CWndUpdateDialogControls(MfcCWndCompat& window, MfcCWndCompat* target,
    bool disable_if_no_handler) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return;
    }

    HWND target_window = target != nullptr ? target->window : window.window;
    for (HWND child = GetTopWindow(window.window); child != nullptr;
         child = GetNextWindow(child, GW_HWNDNEXT)) {
        const UINT control_id = static_cast<UINT>(GetDlgCtrlID(child)) & 0xffffU;
        const bool has_child_window = CWndFromHandlePermanent(child) != nullptr;
        const bool has_target = target_window != nullptr && IsWindow(target_window);
        if (!has_child_window && disable_if_no_handler && control_id != 0 &&
            has_target) {
            EnableWindow(child, FALSE);
        }
    }
}

bool CWndPreTranslateInput(MfcCWndCompat& window, MSG& message) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    if (!is_dialog_input_message(message.message)) {
        return false;
    }
    return IsDialogMessageA(window.window, &message) != 0;
}

bool PreTranslateMessage(MfcCWndCompat& window, MSG& message) {
    return CWndPreTranslateInput(window, message);
}

int CWndRunModalLoop(MfcCWndCompat& window, unsigned flags) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return -1;
    }

    bool do_idle = true;
    int idle_count = 0;
    bool show_on_idle = (flags & 4U) != 0 &&
        (GetWindowLongA(window.window, GWL_STYLE) & WS_VISIBLE) == 0;
    HWND parent = GetParent(window.window);

    window.modal_flags |= kMfcModalContinueFlag | kMfcModalLoopFlag;
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    MSG local_message{};
    MSG* message = thread != nullptr ? &thread->current_message : &local_message;

    for (;;) {
        if (!CWndContinueModal(window)) {
            window.modal_flags &= ~(kMfcModalContinueFlag | kMfcModalLoopFlag);
            return window.modal_result;
        }

        while (do_idle &&
            PeekMessageA(message, nullptr, 0, 0, PM_NOREMOVE) == 0) {
            if (!CWndContinueModal(window)) {
                window.modal_flags &= ~(kMfcModalContinueFlag | kMfcModalLoopFlag);
                return window.modal_result;
            }

            if (show_on_idle) {
                modal_show_on_idle(window);
                show_on_idle = false;
            }

            if ((flags & 1U) == 0 && parent != nullptr && idle_count == 0) {
                SendMessageA(parent, WM_ENTERIDLE, 0,
                    reinterpret_cast<LPARAM>(window.window));
            }

            if ((flags & 2U) != 0) {
                do_idle = false;
                break;
            }

            if (SendMessageA(window.window, kMfcKickIdleMessage, 0,
                    idle_count) == 0) {
                do_idle = false;
            }
            ++idle_count;
        }

        if (thread != nullptr) {
            if (!WinThreadPumpMessage(*thread)) {
                PostQuitMessage(0);
                window.modal_flags &= ~(kMfcModalContinueFlag | kMfcModalLoopFlag);
                return -1;
            }
            if (show_on_idle &&
                (thread->current_message.message == WM_NCLBUTTONUP ||
                    thread->current_message.message == WM_SYSKEYDOWN)) {
                modal_show_on_idle(window);
                show_on_idle = false;
            }
            if (!CWndContinueModal(window)) {
                window.modal_flags &= ~(kMfcModalContinueFlag | kMfcModalLoopFlag);
                return window.modal_result;
            }
            if (WinThreadIsIdleMessage(*thread, thread->current_message)) {
                do_idle = true;
                idle_count = 0;
            }
        } else {
            const BOOL got_message = GetMessageA(message, nullptr, 0, 0);
            if (got_message <= 0) {
                PostQuitMessage(0);
                window.modal_flags &= ~(kMfcModalContinueFlag | kMfcModalLoopFlag);
                return -1;
            }
            if (!CWndPreTranslateInput(window, *message)) {
                TranslateMessage(message);
                DispatchMessageA(message);
            }
            do_idle = true;
            idle_count = 0;
        }
    }
}

bool CWndContinueModal(const MfcCWndCompat& window) {
    return (window.modal_flags & kMfcModalContinueFlag) != 0;
}

void CWndEndModalLoop(MfcCWndCompat& window, int result) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return;
    }
    window.modal_result = result;
    if ((window.modal_flags & kMfcModalContinueFlag) != 0) {
        window.modal_flags &= ~kMfcModalContinueFlag;
        PostMessageA(window.window, WM_NULL, 0, 0);
    }
}

bool CWndUnsupportedModalOperation() {
    if (AfxAssertFailedLine("wincore.cpp", 0xdcf)) {
        CrtDebugBreak();
    }
    return false;
}

bool AfxRegisterIconClass(WNDCLASSA& window_class, const char* class_name,
    UINT class_style, UINT icon_id) {
    window_class.style = class_style;
    window_class.lpfnWndProc = DefWindowProcA;
    window_class.hInstance = GetModuleHandleA(nullptr);
    window_class.lpszClassName = class_name;
    window_class.hIcon = LoadIconA(window_class.hInstance,
        MAKEINTRESOURCEA(icon_id & 0xffffU));
    if (window_class.hIcon == nullptr) {
        window_class.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    }
    return AfxRegisterClassCompat(window_class);
}

unsigned AfxInitCommonControlsClass(INITCOMMONCONTROLSEX& init,
    unsigned requested) {
    if (requested == 0) {
        return 0;
    }

    HMODULE already_loaded = GetModuleHandleA("COMCTL32.DLL");
    HMODULE library = LoadLibraryA("COMCTL32.DLL");
    if (library == nullptr) {
        return 0;
    }

    unsigned registered = 0;
    using InitCommonControlsExProc = BOOL (WINAPI *)(const INITCOMMONCONTROLSEX*);
    auto init_common_controls_ex = reinterpret_cast<InitCommonControlsExProc>(
        GetProcAddress(library, "InitCommonControlsEx"));
    if (init_common_controls_ex == nullptr) {
        if ((requested & 0x3fc0U) == requested) {
            InitCommonControls();
            registered = 0x3fc0U;
        }
    } else {
        init.dwSize = sizeof(INITCOMMONCONTROLSEX);
        if (init_common_controls_ex(&init) != FALSE) {
            registered = requested;
            if (already_loaded == nullptr) {
                InitCommonControls();
                registered |= 0x3fc0U;
            }
        }
    }

    FreeLibrary(library);
    return registered;
}

bool AfxDeferRegisterClass(unsigned flags) {
    flags &= ~g_deferred_registered_classes;
    if (flags == 0) {
        return true;
    }

    unsigned registered = 0;
    WNDCLASSA window_class{};
    window_class.lpfnWndProc = DefWindowProcA;
    window_class.hInstance = GetModuleHandleA(nullptr);
    window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(static_cast<ULONG_PTR>(COLOR_WINDOW + 1));

    if ((flags & 1U) != 0) {
        window_class.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
        window_class.cbWndExtra = 0;
        window_class.lpszClassName = "AfxWnd42sd";
        if (AfxRegisterClassCompat(window_class)) {
            registered |= 1U;
        }
    }
    if ((flags & 0x20U) != 0) {
        window_class.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS | CS_OWNDC;
        window_class.cbWndExtra = 0;
        window_class.lpszClassName = "AfxOleControl42sd";
        if (AfxRegisterClassCompat(window_class)) {
            registered |= 0x20U;
        }
    }
    if ((flags & 2U) != 0) {
        window_class.style = 0;
        window_class.cbWndExtra = sizeof(void*) * 2;
        window_class.lpszClassName = "AfxControlBar42sd";
        if (AfxRegisterClassCompat(window_class)) {
            registered |= 2U;
        }
    }
    if ((flags & 4U) != 0) {
        window_class.cbWndExtra = 0;
        if (AfxRegisterIconClass(window_class, "AfxMDIFrame42sd",
                CS_DBLCLKS, 0x7a01)) {
            registered |= 4U;
        }
    }
    if ((flags & 8U) != 0) {
        window_class.cbWndExtra = sizeof(void*) + sizeof(WORD);
        if (AfxRegisterIconClass(window_class, "AfxFrameOrView42sd",
                CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS, 0x7a02)) {
            registered |= 8U;
        }
    }

    INITCOMMONCONTROLSEX init{sizeof(INITCOMMONCONTROLSEX), 0};
    if ((flags & 0x10U) != 0) {
        init.dwICC = 0xffU;
        registered |= AfxInitCommonControlsClass(init, 0x3fc0U);
        flags &= 0xffffc03fU;
    }

    const struct {
        unsigned class_flag;
        DWORD icc_flag;
    } common_controls[] = {
        {0x40U, ICC_UPDOWN_CLASS},
        {0x80U, ICC_TREEVIEW_CLASSES},
        {0x100U, ICC_TAB_CLASSES},
        {0x200U, ICC_PROGRESS_CLASS},
        {0x400U, ICC_LISTVIEW_CLASSES},
        {0x800U, ICC_HOTKEY_CLASS},
        {0x1000U, ICC_BAR_CLASSES},
        {0x2000U, ICC_ANIMATE_CLASS},
        {0x4000U, ICC_INTERNET_CLASSES},
        {0x8000U, ICC_COOL_CLASSES},
        {0x10000U, ICC_USEREX_CLASSES},
        {0x20000U, ICC_DATE_CLASSES},
    };

    for (const auto& control : common_controls) {
        if ((flags & control.class_flag) == 0) {
            continue;
        }
        init.dwICC = control.icc_flag;
        registered |= AfxInitCommonControlsClass(init, control.class_flag);
    }

    g_deferred_registered_classes |= registered;
    if ((g_deferred_registered_classes & 0x3fc0U) == 0x3fc0U) {
        g_deferred_registered_classes |= 0x10U;
        registered |= 0x10U;
    }
    return (flags & registered) == flags;
}

bool CFrameWndDefaultFalse() {
    return false;
}

bool CFrameWndDefaultTrue() {
    return true;
}

void InitializeFrameWndRectDefaultThunk() {
    InitializeFrameWndRectDefault();
}

void InitializeFrameWndRectDefault() {
    g_frame_rect_default = RECT{LONG_MIN, LONG_MIN, 0, 0};
}

void InitializeMouseWheelMessageThunk() {
    RegisterMouseWheelMessageCompat();
}

UINT RegisterMouseWheelMessageCompat() {
    if (g_registered_mouse_wheel_message == 0) {
        g_registered_mouse_wheel_message =
            RegisterWindowMessageA("MSWHEEL_ROLLMSG");
    }
    return g_registered_mouse_wheel_message;
}

MfcRuntimeClassCompat* GetFrameWndRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CFrameWnd", static_cast<int>(sizeof(MfcFrameWndCompat)), 0xffff,
        +[]() -> void* {
            auto* frame = new MfcFrameWndCompat();
            ConstructFrameWnd(*frame);
            return frame;
        },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcFrameWndCompat& ConstructFrameWnd(MfcFrameWndCompat& frame) {
    ConstructCWnd(frame);
    frame.runtime_class = GetFrameWndRuntimeClass();
    frame.accelerator = nullptr;
    frame.active_view = nullptr;
    frame.disabled_modal_windows.clear();
    frame.modal_disable_count = 0;
    frame.tracking = false;
    frame.idle_pending = false;
    frame.auto_menu_enable = true;
    frame.in_help_mode = false;
    frame.help_context = 0;
    frame.tracking_help_context = 0;
    frame.title.clear();
    frame.menu = nullptr;
    frame.mdi_client = nullptr;
    frame.control_bars.clear();
    frame.floating_frame_class = nullptr;
    frame.active_document = nullptr;
    frame.preview_mode_active = false;
    frame.command_target = nullptr;
    FrameWndAddFrameWnd(frame);
    return frame;
}

static MfcCommandTargetCompat* FrameWndEnsureCommandTarget(
    MfcFrameWndCompat& frame) {
    auto* target = static_cast<MfcCommandTargetCompat*>(frame.command_target);
    if (target == nullptr) {
        target = new MfcCommandTargetCompat();
        ConstructCmdTarget(*target);
        frame.command_target = target;
    }
    target->owner = &frame;
    return target;
}

static void FrameWndDestroyCommandTarget(MfcFrameWndCompat& frame) {
    auto* target = static_cast<MfcCommandTargetCompat*>(frame.command_target);
    if (target == nullptr) {
        return;
    }
    DestroyCmdTarget(*target);
    delete target;
    frame.command_target = nullptr;
}

void DestroyFrameWnd(MfcFrameWndCompat& frame) {
    FrameWndRemoveFrameWnd(frame);
    FrameWndEndModalState(frame);
    FrameWndDestroyCommandTarget(frame);
    frame.active_view = nullptr;
    frame.accelerator = nullptr;
    frame.menu = nullptr;
    frame.mdi_client = nullptr;
    frame.control_bars.clear();
    frame.floating_frame_class = nullptr;
    frame.active_document = nullptr;
    DestroyCWndCompat(frame);
}

MfcFrameWndCompat* DeleteFrameWndScalarDtor(MfcFrameWndCompat* frame,
    unsigned flags) {
    if (frame == nullptr) {
        return nullptr;
    }
    DestroyFrameWnd(*frame);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(frame);
    }
    return frame;
}

void FrameWndAddFrameWnd(MfcFrameWndCompat& frame) {
    if (std::find(g_frame_windows.begin(), g_frame_windows.end(), &frame) ==
        g_frame_windows.end()) {
        g_frame_windows.push_back(&frame);
    }
}

void FrameWndRemoveFrameWnd(MfcFrameWndCompat& frame) {
    g_frame_windows.erase(std::remove(g_frame_windows.begin(),
        g_frame_windows.end(), &frame), g_frame_windows.end());
}

bool FrameWndLoadAccelTable(MfcFrameWndCompat& frame,
    const char* resource_name) {
    if (frame.accelerator != nullptr || resource_name == nullptr) {
        AfxTraceOutput("winfrm.cpp(0xa4): invalid LoadAccelTable state.\n");
        return false;
    }
    frame.accelerator = LoadAcceleratorsA(GetModuleHandleA(nullptr),
        resource_name);
    return frame.accelerator != nullptr;
}

HACCEL FrameWndGetDefaultAccelerator(MfcFrameWndCompat& frame) {
    return frame.accelerator;
}

bool FrameWndPreTranslateMessage(MfcFrameWndCompat& frame, MSG& message) {
    if (message.message == WM_LBUTTONDOWN ||
        message.message == WM_NCLBUTTONDOWN) {
        CloseFocusedComboBoxDropDown(message.hwnd);
    }
    if (frame.active_view != nullptr &&
        frame.active_view->window != nullptr &&
        AfxPreTranslateMessageFromWindow(frame.active_view->window, message)) {
        return true;
    }
    HACCEL accelerator = FrameWndGetDefaultAccelerator(frame);
    if (accelerator != nullptr && frame.window != nullptr &&
        message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST &&
        TranslateAcceleratorA(frame.window, accelerator, &message) != 0) {
        return true;
    }
    return false;
}

bool PreTranslateMessage(MfcFrameWndCompat& frame, MSG& message) {
    return FrameWndPreTranslateMessage(frame, message);
}

void FrameWndPostNcDestroy(MfcFrameWndCompat* frame) {
    delete frame;
}

void FrameWndOnPaletteChanged(MfcFrameWndCompat& frame, HWND changed_window) {
    (void)frame;
    (void)changed_window;
}

bool FrameWndOnQueryNewPalette(MfcFrameWndCompat& frame) {
    (void)frame;
    return false;
}

void FrameWndCancelMode(MfcFrameWndCompat& frame) {
    if (!frame.tracking || frame.window == nullptr) {
        return;
    }
    PostMessageA(frame.window, 0x0367, 0, 0);
    if (GetCapture() == frame.window) {
        ReleaseCapture();
    }
    frame.tracking = false;
    SendMessageA(frame.window, kMfcKickIdleMessage, 0, 0);
}

bool FrameWndOnCommand(MfcFrameWndCompat& frame, WPARAM wparam,
    LPARAM lparam) {
    const UINT command = LOWORD(wparam);
    if (!frame.tracking || lparam != 0 || command == kMfcHelpCommand ||
        command == 0xe147 || command == 0xe145) {
        return CWndOnCommand(frame, wparam, reinterpret_cast<HWND>(lparam));
    }
    if (frame.window != nullptr &&
        SendMessageA(frame.window, 0x0365, 0,
            static_cast<LPARAM>(command + 0x10000)) == 0) {
        SendMessageA(frame.window, WM_COMMAND, 0xe147, 0);
    }
    return true;
}

bool FrameWndIsDescendant(HWND ancestor, HWND child) {
    if (ancestor == nullptr || child == nullptr || !IsWindow(ancestor) ||
        !IsWindow(child)) {
        return false;
    }
    for (HWND current = child; current != nullptr;
         current = GetWindowOwnerOrParent(current)) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

void FrameWndBeginModalState(MfcFrameWndCompat& frame) {
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return;
    }
    ++frame.modal_disable_count;
    if (frame.modal_disable_count > 1) {
        return;
    }
    HWND root = GetAncestor(frame.window, GA_ROOT);
    for (HWND window = GetWindow(GetDesktopWindow(), GW_CHILD);
         window != nullptr; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == frame.window || !IsWindowEnabled(window) ||
            !FrameWndIsDescendant(root, window)) {
            continue;
        }
        if (SendMessageA(window, 0x036c, 0, 0) == 0) {
            EnableWindow(window, FALSE);
            frame.disabled_modal_windows.push_back(window);
        }
    }
}

void FrameWndEndModalState(MfcFrameWndCompat& frame) {
    if (frame.modal_disable_count <= 0) {
        return;
    }
    --frame.modal_disable_count;
    if (frame.modal_disable_count != 0) {
        return;
    }
    for (HWND window : frame.disabled_modal_windows) {
        if (window != nullptr && IsWindow(window)) {
            EnableWindow(window, TRUE);
        }
    }
    frame.disabled_modal_windows.clear();
}

void FrameWndShowOwnedWindows(MfcFrameWndCompat& frame, bool show) {
    if (frame.window == nullptr) {
        return;
    }
    HWND root = GetAncestor(frame.window, GA_ROOT);
    for (HWND window = GetWindow(GetDesktopWindow(), GW_CHILD);
         window != nullptr; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == frame.window || !FrameWndIsDescendant(root, window)) {
            continue;
        }
        LONG style = GetWindowLongA(window, GWL_STYLE);
        if (!show && (style & (WS_VISIBLE | WS_MINIMIZE)) == WS_VISIBLE) {
            ShowWindow(window, SW_HIDE);
        } else if (show && (style & (WS_VISIBLE | WS_MINIMIZE)) == 0) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
        }
    }
}

void FrameWndOnEnable(MfcFrameWndCompat& frame, BOOL enabled) {
    if (!enabled) {
        FrameWndBeginModalState(frame);
    } else {
        FrameWndEndModalState(frame);
    }
    FrameWndNotifyFloatingWindows(frame, enabled ? 0x10 : 0x20);
}

void FrameWndNotifyFloatingWindows(MfcFrameWndCompat& frame, DWORD flags) {
    if (frame.window == nullptr) {
        return;
    }
    MfcFrameWndCompat* top_frame = &frame;
    if ((CWndGetStyle(frame) & WS_CHILD) != 0) {
        top_frame = GetTopLevelFrame(frame);
        if (top_frame == nullptr) {
            return;
        }
    }
    if ((flags & 0x0cU) != 0) {
        if ((flags & 0x08U) == 0 && IsWindowEnabled(frame.window) != FALSE &&
            top_frame != &frame) {
            frame.wnd_flags |= kMfcWndFlagKeepMiniActive;
            SendMessageA(frame.window, WM_NCACTIVATE, TRUE, 0);
            frame.wnd_flags &= ~kMfcWndFlagKeepMiniActive;
        } else {
            SendMessageA(frame.window, WM_NCACTIVATE, FALSE, 0);
        }
    }
    HWND root = top_frame->window;
    for (HWND window = GetWindow(GetDesktopWindow(), GW_CHILD);
         window != nullptr; window = GetWindow(window, GW_HWNDNEXT)) {
        if (FrameWndIsDescendant(root, window)) {
            SendMessageA(window, kMfcFloatingFrameMessage, flags, 0);
        }
    }
}

bool FrameWndPreCreateWindow(MfcFrameWndCompat& frame, CREATESTRUCTA& create) {
    (void)frame;
    if (create.lpszClass == nullptr) {
        if (!AfxDeferRegisterClass(8)) {
            return false;
        }
        create.lpszClass = "AfxFrameOrView42sd";
    }
    return true;
}

bool FrameWndCreateEx(MfcFrameWndCompat& frame, DWORD ex_style,
    const char* class_name, const char* title, DWORD style,
    const RECT& rect, HWND parent, const char* menu_resource, void* param) {
    HMENU menu = nullptr;
    if (menu_resource != nullptr) {
        menu = LoadMenuA(GetModuleHandleA(nullptr), menu_resource);
        if (menu == nullptr) {
            AfxTraceOutput("Warning: failed to load menu for CFrameWnd.\n");
            return false;
        }
    }
    const bool created = CreateWindowExFromRect(frame, ex_style,
        class_name == nullptr ? "AfxFrameOrView42sd" : class_name,
        title == nullptr ? "" : title, style, rect, parent, menu, param);
    if (!created) {
        AfxTraceOutput("Warning: failed to create CFrameWnd.\n");
        if (menu != nullptr) {
            DestroyMenu(menu);
        }
        return false;
    }
    frame.title = title == nullptr ? "" : title;
    frame.menu = menu != nullptr ? menu : GetMenu(frame.window);
    return true;
}

MfcViewCompat* FrameWndCreateView(MfcFrameWndCompat& frame,
    MfcRuntimeClassCompat* view_class, UINT child_id,
    MfcCreateContextCompat* context) {
    if (frame.window == nullptr || view_class == nullptr ||
        view_class->create_object == nullptr) {
        return nullptr;
    }
    void* object = RuntimeClassCreateObject(*view_class);
    auto* base = static_cast<MfcObjectCompat*>(object);
    if (base == nullptr ||
        !ObjectIsKindOfRuntimeClass(base, GetViewRuntimeClass())) {
        delete base;
        AfxTraceOutput("Warning: dynamic create of view failed.\n");
        return nullptr;
    }
    auto* view = static_cast<MfcViewCompat*>(base);
    RECT rect{0, 0, 0, 0};
    if (!CreateWindowExFromRect(*view, 0, "AfxFrameOrView42sd", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER, rect, frame.window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(child_id)),
            context)) {
        delete view;
        AfxTraceOutput("Warning: could not create view for frame.\n");
        return nullptr;
    }
    if ((GetWindowLongA(view->window, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0) {
        CWndModifyStyleEx(*view, WS_EX_CLIENTEDGE, 0, SWP_FRAMECHANGED);
    }
    view->active_frame = &frame;
    if (context != nullptr) {
        view->document = context->current_doc;
        context->last_view = view;
    }
    frame.active_view = view;
    return view;
}

bool FrameWndOnCreateClient(MfcFrameWndCompat& frame, CREATESTRUCTA* create,
    MfcCreateContextCompat* context) {
    (void)create;
    if (context != nullptr && context->new_view_class != nullptr) {
        return FrameWndCreateView(frame,
            static_cast<MfcRuntimeClassCompat*>(context->new_view_class),
            0xe900, context) != nullptr;
    }
    return true;
}

int FrameWndOnCreate(MfcFrameWndCompat& frame, CREATESTRUCTA* create,
    MfcCreateContextCompat* context) {
    if (!FrameWndOnCreateClient(frame, create, context)) {
        AfxTraceOutput("Failed to create client pane/view for frame.\n");
        return -1;
    }
    if (frame.window != nullptr) {
        PostMessageA(frame.window, kMfcSetMessageStringMessage, 0xe001, 0);
    }
    FrameWndOnUpdateFrameTitle(frame, true);
    return 0;
}

const char* FrameWndLoadFrameIconClass(MfcFrameWndCompat& frame,
    DWORD class_style, UINT resource_id) {
    (void)frame;
    HICON icon = LoadIconA(GetModuleHandleA(nullptr),
        MAKEINTRESOURCEA(resource_id));
    if (icon == nullptr) {
        return nullptr;
    }
    return AfxRegisterWndClassCompat(class_style, nullptr, nullptr, icon);
}

bool FrameWndLoadFrame(MfcFrameWndCompat& frame, UINT resource_id,
    DWORD style, HWND parent, MfcCreateContextCompat* context) {
    if (resource_id == 0 || resource_id > 0x7fffU) {
        CrtDbgReport(2, "winfrm.cpp", 0x2ac, nullptr,
            "invalid CFrameWnd frame resource id");
        return false;
    }
    if (frame.help_context != 0 && frame.help_context != resource_id) {
        CrtDbgReport(2, "winfrm.cpp", 0x2ad, nullptr,
            "CFrameWnd frame resource id changed");
    }
    char title[256]{};
    LoadStringA(GetModuleHandleA(nullptr), resource_id, title,
        static_cast<int>(sizeof(title)));
    title[std::strcspn(title, "\n")] = '\0';
    const char* class_name = FrameWndLoadFrameIconClass(frame,
        CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, resource_id);
    RECT rect = g_frame_rect_default;
    if (!FrameWndCreateEx(frame, 0, class_name, title, style, rect, parent,
            MAKEINTRESOURCEA(resource_id), context)) {
        return false;
    }
    frame.help_context = resource_id;
    FrameWndLoadAccelTable(frame, MAKEINTRESOURCEA(resource_id));
    if (context == nullptr) {
        CWndSendMessageToDescendantsInline(frame, kMfcInitialUpdateMessage, 0, 0);
    }
    return true;
}

void FrameWndOnUpdateFrameMenu(MfcFrameWndCompat& frame, HMENU menu) {
    if (menu == nullptr) {
        menu = frame.menu;
    }
    if (frame.window != nullptr) {
        SetMenu(frame.window, menu);
    }
}

MfcViewCompat* FrameWndEnsureActiveView(MfcFrameWndCompat& frame) {
    MfcViewCompat* view = FrameWndGetActiveView(frame);
    if (view != nullptr) {
        return view;
    }
    MfcCWndCompat* pane = CWndGetDescendantWindowInline(frame, 0xe900, true);
    if (pane != nullptr && ObjectIsKindOfRuntimeClass(pane, GetViewRuntimeClass())) {
        view = static_cast<MfcViewCompat*>(pane);
        FrameWndSetActiveView(frame, view, false);
        return view;
    }
    return nullptr;
}

void FrameWndInitialUpdateFrame(MfcFrameWndCompat& frame, void* document,
    bool make_visible) {
    frame.active_document = document;
    MfcViewCompat* active_view = FrameWndEnsureActiveView(frame);
    if (make_visible && frame.window != nullptr) {
        SendMessageA(frame.window, kMfcInitialUpdateMessage, 0, 0);
        ShowWindow(frame.window, SW_SHOW);
        UpdateWindow(frame.window);
        if (active_view != nullptr) {
            ViewOnInitialUpdate(*active_view);
            ViewOnActivateView(*active_view, true,
                active_view, active_view);
        }
    }
}

void FrameWndOnClose(MfcFrameWndCompat& frame) {
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr && app->main_window == frame.window &&
        !WinAppSaveAllModified(*app)) {
        return;
    }
    CWndDestroyWindow(frame);
}

void FrameWndOnDestroy(MfcFrameWndCompat& frame) {
    FrameWndDestroyControlBars(frame);
    if (frame.menu != nullptr && frame.window != nullptr &&
        GetMenu(frame.window) != frame.menu) {
        SetMenu(frame.window, frame.menu);
        if (GetMenu(frame.window) != frame.menu) {
            CrtDbgReport(2, "winfrm.cpp", 0x35e, nullptr,
                "CFrameWnd failed to restore its saved menu");
        }
    }
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr && app->main_window == frame.window &&
        frame.window != nullptr) {
        WinHelpA(frame.window, nullptr, HELP_QUIT, 0);
    }
    CWndDefaultAndReleaseControlSite(frame);
}

void OnDestroy(MfcFrameWndCompat& frame) {
    FrameWndOnDestroy(frame);
}

bool IsTracking(const MfcFrameWndCompat& frame) {
    return frame.tracking;
}

void FrameWndRemoveControlBar(MfcFrameWndCompat& frame, void* bar) {
    frame.control_bars.erase(std::remove(frame.control_bars.begin(),
        frame.control_bars.end(), bar), frame.control_bars.end());
}

static bool WinAppCommandCallback(MfcCommandTargetCompat& target, UINT id,
    int code, void* extra) {
    (void)code;
    (void)extra;
    auto* app = static_cast<MfcWinAppCompat*>(target.owner);
    if (app == nullptr) {
        return false;
    }
    switch (id) {
    case kMfcIdFileNew:
        WinAppOnFileNew(*app);
        return true;
    case kMfcIdFileOpen:
        WinAppOnFileOpen(*app);
        return true;
    default:
        return false;
    }
}

static const MfcMessageMapCompat* GetWinAppCommandMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_COMMAND, 0, kMfcIdFileNew, kMfcIdFileNew, nullptr, 0,
            WinAppCommandCallback},
        {WM_COMMAND, 0, kMfcIdFileOpen, kMfcIdFileOpen, nullptr, 0,
            WinAppCommandCallback},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{GetCmdTargetMessageMap(), entries};
    return &map;
}

static bool FrameWndIsControlBarCommand(UINT id) {
    return kMfcIdControlBarFirst <= id && id <= kMfcIdControlBarLast;
}

static bool FrameWndIsKeyIndicatorCommand(UINT id) {
    return id == 0xe701 || id == 0xe702 || id == 0xe703 || id == 0xe706;
}

static bool FrameWndSetSelfHandlerInfo(MfcFrameWndCompat& frame,
    MfcCommandHandlerInfoCompat* handler_info) {
    if (handler_info != nullptr) {
        handler_info->target = FrameWndEnsureCommandTarget(frame);
        handler_info->handler = nullptr;
    }
    return true;
}

static bool FrameWndOnUpdateSelfCommand(MfcFrameWndCompat& frame, UINT id,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    if (FrameWndIsControlBarCommand(id)) {
        if (FrameWndGetControlBar(frame, id) == nullptr) {
            if (auto* cmd_ui = static_cast<MfcCmdUICompat*>(extra)) {
                cmd_ui->continue_routing = true;
            }
            return false;
        }
        if (handler_info != nullptr) {
            return FrameWndSetSelfHandlerInfo(frame, handler_info);
        }
        auto* cmd_ui = static_cast<MfcCmdUICompat*>(extra);
        if (cmd_ui == nullptr) {
            return false;
        }
        FrameWndOnUpdateControlBarMenu(frame, *cmd_ui);
        return !cmd_ui->continue_routing;
    }
    if (FrameWndIsKeyIndicatorCommand(id)) {
        if (handler_info != nullptr) {
            return FrameWndSetSelfHandlerInfo(frame, handler_info);
        }
        auto* cmd_ui = static_cast<MfcCmdUICompat*>(extra);
        if (cmd_ui == nullptr) {
            return false;
        }
        FrameWndOnUpdateKeyIndicator(*cmd_ui);
        return !cmd_ui->continue_routing;
    }
    if (id == kMfcHelpCommand) {
        if (AfxGetMainWndCompat() != &frame) {
            if (auto* cmd_ui = static_cast<MfcCmdUICompat*>(extra)) {
                cmd_ui->continue_routing = true;
            }
            return false;
        }
        if (handler_info != nullptr) {
            return FrameWndSetSelfHandlerInfo(frame, handler_info);
        }
        auto* cmd_ui = static_cast<MfcCmdUICompat*>(extra);
        if (cmd_ui == nullptr) {
            return false;
        }
        FrameWndOnUpdateContextHelp(frame, *cmd_ui);
        return !cmd_ui->continue_routing;
    }
    return false;
}

static bool FrameWndOnSelfCmdMsg(MfcFrameWndCompat& frame, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    if (code == -1) {
        return FrameWndOnUpdateSelfCommand(frame, id, extra, handler_info);
    }
    if (code == 0 && FrameWndIsControlBarCommand(id)) {
        if (FrameWndGetControlBar(frame, id) == nullptr) {
            return false;
        }
        if (handler_info != nullptr) {
            return FrameWndSetSelfHandlerInfo(frame, handler_info);
        }
        return FrameWndOnBarCheck(frame, id);
    }
    return false;
}

bool FrameWndOnCmdMsg(MfcFrameWndCompat& frame, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    if (frame.active_view != nullptr &&
        ViewOnCmdMsg(*frame.active_view, id, code, extra, handler_info)) {
        return true;
    }
    if (FrameWndOnSelfCmdMsg(frame, id, code, extra, handler_info)) {
        return true;
    }
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app == nullptr) {
        return false;
    }
    app->command_target.owner = app;
    app->command_target.message_map = GetWinAppCommandMessageMap();
    return CmdTargetOnCmdMsg(app->command_target, id, code, extra,
        handler_info);
}

static WPARAM FrameWndScrollWParamFromCurrentMessage(UINT message, UINT code,
    UINT position) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->current_message.message == message) {
        return thread->current_message.wParam;
    }
    return MAKEWPARAM(code, position);
}

static LPARAM FrameWndScrollLParamFromCurrentMessage(UINT message,
    HWND scroll_bar) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->current_message.message == message) {
        return thread->current_message.lParam;
    }
    return reinterpret_cast<LPARAM>(scroll_bar);
}

void FrameWndOnHScroll(MfcFrameWndCompat& frame, UINT code, UINT position,
    HWND scroll_bar) {
    if (frame.active_view != nullptr && frame.active_view->window != nullptr) {
        SendMessageA(frame.active_view->window, WM_HSCROLL,
            FrameWndScrollWParamFromCurrentMessage(WM_HSCROLL, code, position),
            FrameWndScrollLParamFromCurrentMessage(WM_HSCROLL, scroll_bar));
    }
}

void FrameWndOnVScroll(MfcFrameWndCompat& frame, UINT code, UINT position,
    HWND scroll_bar) {
    if (frame.active_view != nullptr && frame.active_view->window != nullptr) {
        SendMessageA(frame.active_view->window, WM_VSCROLL,
            FrameWndScrollWParamFromCurrentMessage(WM_VSCROLL, code, position),
            FrameWndScrollLParamFromCurrentMessage(WM_VSCROLL, scroll_bar));
    }
}

void FrameWndOnActivateApp(MfcFrameWndCompat& frame, BOOL active,
    DWORD thread_id) {
    (void)thread_id;
    CWndOnActivateApp(frame);
    FrameWndOnActivateFrame(frame, active != FALSE);
    if (frame.active_view != nullptr) {
        ViewOnActivateView(*frame.active_view, active != FALSE,
            frame.active_view, frame.active_view);
    }
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->main_window == frame.window) {
        MfcViewCompat* view = FrameWndEnsureActiveView(frame);
        if (view != nullptr) {
            ViewOnActivateView(*view, false, view, view);
        }
    }
    if (frame.window != nullptr) {
        SendMessageA(frame.window, kMfcKickIdleMessage, 0, 0);
    }
}

void FrameWndOnActivate(MfcFrameWndCompat& frame, UINT state,
    HWND other_window, BOOL minimized) {
    if (frame.window != nullptr) {
        DefWindowProcA(frame.window, WM_ACTIVATE, state,
            reinterpret_cast<LPARAM>(other_window));
    }
    MfcFrameWndCompat* top_frame = &frame;
    if ((CWndGetStyle(frame) & WS_CHILD) != 0) {
        top_frame = GetTopLevelFrame(frame);
        if (top_frame == nullptr) {
            return;
        }
    }

    MfcCWndCompat* compare_window = &frame;
    if (state == WA_INACTIVE) {
        compare_window = other_window == nullptr ? nullptr :
            CWndFromHandle(other_window);
    }
    bool active = false;
    if (compare_window != nullptr) {
        MfcFrameWndCompat* compare_frame =
            ObjectIsKindOfRuntimeClass(compare_window, GetFrameWndRuntimeClass())
            ? static_cast<MfcFrameWndCompat*>(compare_window)
            : GetTopLevelFrame(*compare_window);
        active = compare_window == top_frame ||
            (compare_frame == top_frame &&
                (compare_window == top_frame ||
                    (compare_window->window != nullptr &&
                        SendMessageA(compare_window->window,
                            kMfcFloatingFrameMessage, 0x40, 0) != 0)));
    }
    top_frame->wnd_flags &= ~kMfcWndFlagTopLevelActive;
    if (active) {
        top_frame->wnd_flags |= kMfcWndFlagTopLevelActive;
    }
    FrameWndNotifyFloatingWindows(frame, active ? 0x04 : 0x08);
    MfcViewCompat* view = FrameWndEnsureActiveView(frame);
    if (view != nullptr) {
        if (state != WA_INACTIVE && minimized == FALSE) {
            ViewOnActivateView(*view, true, view, view);
        }
        ViewOnActivateFrameDefault(*view, state, &frame);
    }
}

void FrameWndOnActivateFrame(MfcFrameWndCompat& frame, bool active) {
    if ((frame.wnd_flags & kMfcWndFlagTopLevelActive) != 0) {
        active = true;
    }
    if (frame.window != nullptr && IsWindowEnabled(frame.window) == FALSE) {
        active = false;
    }
    if (frame.window != nullptr) {
        SendMessageA(frame.window, WM_NCACTIVATE, active ? TRUE : FALSE, 0);
    }
}

void FrameWndOnSysCommand(MfcFrameWndCompat& frame, UINT command,
    LPARAM lparam) {
    MfcFrameWndCompat* top_frame = GetTopLevelFrame(frame);
    MfcFrameWndCompat& tracking_frame =
        top_frame == nullptr ? frame : *top_frame;
    const UINT system_command = command & 0xfff0U;
    if (!tracking_frame.tracking) {
        CWndOnSysCommand(frame, command, lparam);
        return;
    }
    switch (system_command) {
    case SC_SIZE:
    case SC_MOVE:
    case SC_MINIMIZE:
    case SC_MAXIMIZE:
    case SC_CLOSE:
    case SC_RESTORE:
    case SC_TASKLIST:
        if (frame.window != nullptr &&
            SendMessageA(frame.window, 0x0365, 0,
                static_cast<LPARAM>(((system_command - SC_SIZE) >> 4) +
                    0x1ef00)) == 0) {
            SendMessageA(frame.window, WM_COMMAND, 0xe147, 0);
        }
        break;
    default:
        CWndOnSysCommand(frame, command, lparam);
        break;
    }
}

void FrameWndOnDropFiles(MfcFrameWndCompat& frame, HDROP drop) {
    (void)frame;
    if (drop == nullptr) {
        return;
    }
    char path[MAX_PATH]{};
    const UINT count = DragQueryFileA(drop, 0xffffffff, nullptr, 0);
    for (UINT index = 0; index < count; ++index) {
        if (DragQueryFileA(drop, index, path, static_cast<UINT>(sizeof(path))) != 0) {
            AfxTraceOutput("Dropped file: %s\n", path);
        }
    }
    DragFinish(drop);
}

bool FrameWndOnQueryEndSession(MfcFrameWndCompat& frame) {
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app == nullptr || app->main_window != frame.window) {
        return true;
    }
    return WinAppSaveAllModified(*app);
}

void FrameWndOnEndSession(MfcFrameWndCompat& frame, BOOL ending) {
    if (ending == FALSE) {
        return;
    }
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app == nullptr || app->main_window != frame.window) {
        return;
    }
    AfxOleSetUserCtrl(true);
    if (app->doc_manager != nullptr) {
        DocManagerCloseAllDocuments(
            *static_cast<MfcDocManagerCompat*>(app->doc_manager), true);
    }
    PostQuitMessage(0);
}

LRESULT FrameWndOnDDEInitiate(MfcFrameWndCompat& frame, HWND client,
    LPARAM atoms) {
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app == nullptr || client == nullptr || frame.window == nullptr) {
        return 0;
    }
    const ATOM app_atom = LOWORD(atoms);
    const ATOM topic_atom = HIWORD(atoms);
    if (app_atom == 0 || topic_atom == 0) {
        return 0;
    }

    char app_name[260]{};
    char topic_name[260]{};
    if (GlobalGetAtomNameA(app_atom, app_name,
            static_cast<int>(sizeof(app_name))) == 0 ||
        GlobalGetAtomNameA(topic_atom, topic_name,
            static_cast<int>(sizeof(topic_name))) == 0) {
        return 0;
    }
    const std::string expected_app =
        app->app_name.empty() ? app->exe_name : app->app_name;
    const bool app_matches = expected_app.empty() ||
        lstrcmpiA(app_name, expected_app.c_str()) == 0 ||
        lstrcmpiA(app_name, "*") == 0;
    const bool topic_matches = lstrcmpiA(topic_name, "System") == 0 ||
        lstrcmpiA(topic_name, "*") == 0;
    if (!app_matches || !topic_matches) {
        return 0;
    }

    ATOM reply_app = GlobalAddAtomA(app_name);
    ATOM reply_topic = GlobalAddAtomA(topic_name);
    SendMessageA(client, WM_DDE_ACK, reinterpret_cast<WPARAM>(frame.window),
        MAKELPARAM(reply_app, reply_topic));
    return 0;
}

LRESULT FrameWndOnDDEExecute(MfcFrameWndCompat& frame, HWND client,
    LPARAM dde_lparam) {
    UINT_PTR unused = 0;
    HGLOBAL command_data = nullptr;
    if (!UnpackDDElParam(WM_DDE_EXECUTE, dde_lparam, &unused,
            reinterpret_cast<PUINT_PTR>(&command_data))) {
        return 0;
    }

    char command[520]{};
    if (const char* locked = static_cast<const char*>(GlobalLock(command_data))) {
        lstrcpynA(command, locked, static_cast<int>(sizeof(command)));
        GlobalUnlock(command_data);
    }

    LPARAM ack_lparam = ReuseDDElParam(dde_lparam, WM_DDE_EXECUTE, WM_DDE_ACK,
        0x8000, reinterpret_cast<UINT_PTR>(command_data));
    if (client != nullptr && frame.window != nullptr) {
        PostMessageA(client, WM_DDE_ACK, reinterpret_cast<WPARAM>(frame.window),
            ack_lparam);
    }

    if (frame.window != nullptr && IsWindowEnabled(frame.window) == FALSE) {
        AfxTraceOutput("Warning: DDE command '%s' ignored; frame disabled.\n",
            command);
        return 0;
    }
    MfcWinAppCompat* app = AfxGetAppCompat();
    auto* manager = app == nullptr ? nullptr
        : static_cast<MfcDocManagerCompat*>(app->doc_manager);
    if (manager != nullptr && !DocManagerOnDDECommand(*manager, command)) {
        AfxTraceOutput("Error: failed to execute DDE command '%s'.\n",
            command);
    }
    return 0;
}

LRESULT FrameWndOnDDETerminate(MfcFrameWndCompat& frame, HWND client,
    LPARAM dde_lparam) {
    if (client != nullptr && frame.window != nullptr) {
        PostMessageA(client, WM_DDE_TERMINATE,
            reinterpret_cast<WPARAM>(frame.window), dde_lparam);
    }
    return 0;
}

MfcViewCompat* FrameWndGetActiveView(MfcFrameWndCompat& frame) {
    if (frame.active_view != nullptr &&
        !ObjectIsKindOfRuntimeClass(frame.active_view, GetViewRuntimeClass())) {
        AfxTraceOutput("Warning: CFrameWnd active view is not a CView.\n");
        return nullptr;
    }
    return frame.active_view;
}

void FrameWndSetActiveView(MfcFrameWndCompat& frame, MfcViewCompat* view,
    bool notify) {
    if (view != nullptr &&
        !ObjectIsKindOfRuntimeClass(view, GetViewRuntimeClass())) {
        AfxTraceOutput("Warning: SetActiveView received a non-view object.\n");
        return;
    }
    MfcViewCompat* previous = frame.active_view;
    if (previous == view) {
        return;
    }
    frame.active_view = nullptr;
    if (previous != nullptr) {
        ViewOnActivateView(*previous, false, view, previous);
    }
    frame.active_view = view;
    if (view != nullptr) {
        view->active_frame = &frame;
        frame.active_document = view->document;
        if (notify) {
            ViewOnActivateView(*view, true, view, previous);
        }
    }
}

void FrameWndOnSetFocus(MfcFrameWndCompat& frame, MfcCWndCompat* old_window) {
    (void)old_window;
    if (frame.active_view != nullptr && frame.active_view->window != nullptr) {
        CWndSetFocus(*frame.active_view);
    }
}

void* FrameWndGetActiveDocument(MfcFrameWndCompat& frame) {
    MfcViewCompat* view = FrameWndGetActiveView(frame);
    return view == nullptr ? frame.active_document : view->document;
}

void FrameWndShowControlBar(MfcFrameWndCompat& frame,
    MfcControlBarCompat* bar, bool show, bool delay) {
    if (bar == nullptr) {
        return;
    }
    if (std::find(frame.control_bars.begin(), frame.control_bars.end(), bar) ==
        frame.control_bars.end()) {
        frame.control_bars.push_back(bar);
    }
    if (delay) {
        ControlBarSetDelayShow(*bar, show);
        PostMessageA(frame.window, kMfcKickIdleMessage, 0, 0);
    } else {
        ControlBarSetDelayShow(*bar, show);
        if (bar->window != nullptr) {
            CWndShowWindow(*bar, show ? SW_SHOW : SW_HIDE);
        }
    }
}

void FrameWndOnWindowPosChanged(MfcFrameWndCompat& frame,
    const WINDOWPOS* position) {
    (void)frame;
    (void)position;
}

void FrameWndOnInitMenuPopup(MfcFrameWndCompat& frame, HMENU menu,
    UINT index, BOOL system_menu) {
    (void)index;
    CloseFocusedComboBoxDropDown(frame.window);
    if (menu == nullptr || system_menu != FALSE) {
        return;
    }
    const int count = GetMenuItemCount(menu);
    for (int item = 0; item < count; ++item) {
        const UINT id = GetMenuItemID(menu, item);
        if (id == 0 || id == 0xffffffffU) {
            continue;
        }
        if (frame.auto_menu_enable && id < 0xf000U) {
            EnableMenuItem(menu, item, MF_BYPOSITION | MF_ENABLED);
        }
    }
}

void FrameWndOnMenuSelect(MfcFrameWndCompat& frame, UINT item_id,
    UINT flags, HMENU menu) {
    (void)menu;
    if (flags == 0xffffU) {
        frame.menu_tracking_active = false;
        frame.tracking_message_id = frame.in_help_mode ? 0xe002 : 0xe001;
        if (frame.window != nullptr) {
            SendMessageA(frame.window, kMfcSetMessageStringMessage,
                frame.tracking_message_id, 0);
        }
        return;
    }

    frame.menu_tracking_active = true;
    if (item_id == 0 || (flags & (MF_SEPARATOR | MF_POPUP)) != 0) {
        frame.tracking_message_id = 0;
    } else if (0xf000U <= item_id && item_id <= 0xf1efU) {
        frame.tracking_message_id = ((item_id - 0xf000U) >> 4) + 0xef00U;
    } else if (item_id < 0xff00U) {
        frame.tracking_message_id = item_id;
    } else {
        frame.tracking_message_id = 0xef1fU;
    }
    if (frame.window != nullptr &&
        frame.tracking_message_id != frame.last_message_id) {
        SendMessageA(frame.window, kMfcKickIdleMessage, 0, 0);
    }
}

std::string FrameWndLoadMessageString(UINT message_id) {
    if (message_id == 0) {
        return {};
    }
    char buffer[512]{};
    if (AfxLoadStringCompat(message_id, buffer, static_cast<int>(sizeof(buffer))) == 0) {
        AfxTraceOutput("Warning: no message line prompt for ID 0x%04x.\n",
            message_id);
        return {};
    }
    std::string text = buffer;
    const std::size_t newline = text.find('\n');
    if (newline != std::string::npos) {
        text.erase(newline);
    }
    return text;
}

LRESULT FrameWndOnSetMessageString(MfcFrameWndCompat& frame,
    UINT message_id, LPARAM text_lparam) {
    if (frame.menu_tracking_active) {
        return 0;
    }
    return FrameWndApplyMessageText(frame, message_id,
        reinterpret_cast<const char*>(text_lparam));
}

UINT FrameWndApplyMessageText(MfcFrameWndCompat& frame, UINT message_id,
    const char* explicit_text) {
    const UINT previous = frame.last_message_id;
    if (explicit_text != nullptr) {
        frame.status_text = explicit_text;
    } else {
        UINT load_message_id = message_id;
        if (load_message_id == 0xef06U && frame.preview_mode_active) {
            load_message_id = 0xf005U;
        }
        frame.status_text = FrameWndLoadMessageString(load_message_id);
        message_id = load_message_id;
    }
    frame.tracking_message_id = message_id;
    frame.last_message_id = message_id;
    if (MfcCWndCompat* message_bar = FrameWndGetMessageBar(frame)) {
        CWndSetWindowText(*message_bar, frame.status_text.c_str());
    }
    return previous;
}

UINT FrameWndSetMessageText(MfcFrameWndCompat& frame, UINT message_id,
    const char* explicit_text) {
    return FrameWndApplyMessageText(frame, message_id, explicit_text);
}

std::vector<void*>& FrameWndGetControlBarList(MfcFrameWndCompat& frame) {
    return frame.control_bars;
}

MfcCWndCompat* FrameWndGetMessageBar(MfcFrameWndCompat& frame) {
    return CWndGetDlgItem(frame, 0xe801);
}

void FrameWndOnEnterIdle(MfcFrameWndCompat& frame, UINT reason,
    MfcCWndCompat* idle_window) {
    (void)idle_window;
    if (reason == MSGF_MENU &&
        frame.tracking_message_id != frame.last_message_id) {
        FrameWndApplyMessageText(frame, frame.tracking_message_id);
    }
}

void FrameWndSetMessageTextRaw(MfcFrameWndCompat& frame, const char* text) {
    FrameWndApplyMessageText(frame, 0, text == nullptr ? "" : text);
}

void FrameWndSetMessageTextById(MfcFrameWndCompat& frame, UINT message_id) {
    FrameWndApplyMessageText(frame, message_id);
}

void FrameWndDestroyControlBars(MfcFrameWndCompat& frame) {
    std::vector<void*> bars = frame.control_bars;
    for (void* entry : bars) {
        auto* bar = static_cast<MfcControlBarCompat*>(entry);
        if (bar == nullptr) {
            continue;
        }
        if (bar->window != nullptr && IsWindow(bar->window)) {
            DestroyWindow(bar->window);
        }
        bar->owner_frame = nullptr;
    }
    frame.control_bars.clear();
}

MfcControlBarCompat* FrameWndGetControlBar(MfcFrameWndCompat& frame, UINT id) {
    if (id == 0) {
        return nullptr;
    }
    for (void* entry : frame.control_bars) {
        auto* bar = static_cast<MfcControlBarCompat*>(entry);
        if (bar != nullptr && bar->window != nullptr &&
            (static_cast<UINT>(GetDlgCtrlID(bar->window)) & 0xffffU) == id) {
            return bar;
        }
    }
    if (frame.window != nullptr) {
        HWND child = GetDlgItem(frame.window, static_cast<int>(id));
        if (child != nullptr) {
            auto* window = CWndFromHandlePermanent(child);
            if (window != nullptr &&
                ObjectIsKindOfRuntimeClass(window, GetControlBarRuntimeClass())) {
                return static_cast<MfcControlBarCompat*>(window);
            }
        }
    }
    return nullptr;
}

void FrameWndOnUpdateControlBarMenu(MfcFrameWndCompat& frame,
    MfcCmdUICompat& cmd_ui) {
    MfcControlBarCompat* bar = FrameWndGetControlBar(frame, cmd_ui.id);
    if (bar == nullptr) {
        cmd_ui.continue_routing = true;
        return;
    }
    CmdUIEnable(cmd_ui, (CWndGetStyle(*bar) & WS_VISIBLE) != 0);
}

bool FrameWndOnBarCheck(MfcFrameWndCompat& frame, UINT id) {
    MfcControlBarCompat* bar = FrameWndGetControlBar(frame, id);
    if (bar == nullptr) {
        return false;
    }
    const bool visible = (CWndGetStyle(*bar) & WS_VISIBLE) != 0;
    FrameWndShowControlBar(frame, bar, !visible, false);
    return true;
}

bool FrameWndOnToolTipText(MfcFrameWndCompat& frame, UINT id, NMHDR* notify,
    LRESULT* result) {
    (void)frame;
    if (notify == nullptr) {
        return false;
    }
    UINT string_id = id;
    if (string_id == 0 && notify->hwndFrom != nullptr) {
        string_id = static_cast<UINT>(GetDlgCtrlID(notify->hwndFrom)) & 0xffffU;
    }
    std::string text = FrameWndLoadMessageString(string_id);
    const std::size_t newline = text.find('\n');
    if (newline != std::string::npos) {
        text.erase(0, newline + 1);
    }
    if (text.empty()) {
        return false;
    }

    if (notify->code == TTN_NEEDTEXTA) {
        auto* tip = reinterpret_cast<NMTTDISPINFOA*>(notify);
        lstrcpynA(tip->szText, text.c_str(),
            static_cast<int>(std::size(tip->szText)));
    } else if (notify->code == TTN_NEEDTEXTW) {
        auto* tip = reinterpret_cast<NMTTDISPINFOW*>(notify);
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, tip->szText,
            static_cast<int>(std::size(tip->szText)));
    } else {
        return false;
    }
    if (result != nullptr) {
        *result = 0;
    }
    return true;
}

void FrameWndOnUpdateKeyIndicator(MfcCmdUICompat& cmd_ui) {
    int virtual_key = 0;
    int mask = 1;
    switch (cmd_ui.id) {
    case 0xe701:
        virtual_key = VK_CAPITAL;
        break;
    case 0xe702:
        virtual_key = VK_NUMLOCK;
        break;
    case 0xe703:
        virtual_key = VK_SCROLL;
        break;
    case 0xe706:
        virtual_key = VK_KANA;
        mask = 0x8000;
        break;
    default:
        AfxTraceOutput("Warning: OnUpdateKeyIndicator unknown id 0x%04x.\n",
            cmd_ui.id);
        cmd_ui.continue_routing = true;
        return;
    }
    CmdUIEnable(cmd_ui, (GetKeyState(virtual_key) & mask) != 0);
}

void FrameWndOnUpdateFrameTitle(MfcFrameWndCompat& frame, bool add_to_title) {
    const char* document_title = nullptr;
    if (add_to_title && frame.active_view != nullptr &&
        frame.active_view->document != nullptr) {
        document_title = "Document";
    }
    FrameWndUpdateFrameTitleForDocument(frame, document_title);
}

void FrameWndUpdateFrameTitleForDocument(MfcFrameWndCompat& frame,
    const char* document_title) {
    std::string base = frame.title.empty() ? "Ranker" : frame.title;
    std::string text;
    const bool prefix_title =
        (frame.window != nullptr &&
            (GetWindowLongA(frame.window, GWL_STYLE) & 0x4000L) != 0);
    if (document_title != nullptr && document_title[0] != '\0') {
        if (prefix_title) {
            text = std::string(document_title) + " - " + base;
        } else {
            text = base + " - " + document_title;
        }
        if (frame.window_number > 0) {
            char suffix[32]{};
            wsprintfA(suffix, ":%d", frame.window_number);
            text += suffix;
        }
    } else {
        text = base;
    }
    if (frame.window != nullptr) {
        SetWindowTextIfChanged(frame.window, text.c_str());
    }
}

void FrameWndOnSetPreviewMode(MfcFrameWndCompat& frame, bool preview,
    MfcPreviewStateCompat& state) {
    if (preview) {
        if (frame.preview_mode_active) {
            AfxTraceOutput("Warning: CFrameWnd preview mode already active.\n");
        }
        frame.preview_mode_active = true;
        state.saved_menu = frame.window == nullptr ? nullptr : GetMenu(frame.window);
        state.saved_focus = GetFocus();
        state.saved_accelerator = frame.accelerator;
        state.visible_control_bar_mask = 0;
        frame.preview_saved_menu = state.saved_menu;
        frame.preview_saved_focus = state.saved_focus;
        frame.preview_saved_accelerator = state.saved_accelerator;
        frame.preview_child_id = state.main_pane_id == 0 ? 0xe900 : state.main_pane_id;
        FrameWndShowOwnedWindows(frame, false);
        for (void* entry : frame.control_bars) {
            auto* bar = static_cast<MfcControlBarCompat*>(entry);
            if (bar == nullptr || bar->window == nullptr) {
                continue;
            }
            const UINT bar_id =
                static_cast<UINT>(GetDlgCtrlID(bar->window)) & 0xffffU;
            if (0xe800U < bar_id && bar_id < 0xe820U) {
                const DWORD bit = 1UL << (bar_id & 0x1fU);
                if ((CWndGetStyle(*bar) & WS_VISIBLE) != 0) {
                    state.visible_control_bar_mask |= bit;
                }
                if (bar_id != state.preview_bar_id) {
                    FrameWndShowControlBar(frame, bar, false, true);
                }
            }
        }
        frame.preview_control_bar_mask = state.visible_control_bar_mask;
        if (frame.window != nullptr) {
            HWND pane = GetDlgItem(frame.window, static_cast<int>(frame.preview_child_id));
            if (pane != nullptr) {
                ShowWindow(pane, SW_HIDE);
            }
            SetMenu(frame.window, nullptr);
        }
        FrameWndLoadAccelTable(frame, MAKEINTRESOURCEA(0x7915));
    } else {
        frame.preview_mode_active = false;
        FrameWndShowOwnedWindows(frame, true);
        if (frame.window != nullptr) {
            SetMenu(frame.window, state.saved_menu != nullptr
                ? state.saved_menu : frame.preview_saved_menu);
            HWND pane = GetDlgItem(frame.window,
                static_cast<int>(frame.preview_child_id));
            if (pane != nullptr) {
                ShowWindow(pane, SW_SHOW);
            }
        }
        frame.accelerator = state.saved_accelerator != nullptr
            ? state.saved_accelerator : frame.preview_saved_accelerator;
        for (void* entry : frame.control_bars) {
            auto* bar = static_cast<MfcControlBarCompat*>(entry);
            if (bar == nullptr || bar->window == nullptr) {
                continue;
            }
            const UINT bar_id =
                static_cast<UINT>(GetDlgCtrlID(bar->window)) & 0xffffU;
            if (0xe800U < bar_id && bar_id < 0xe820U) {
                const DWORD bit = 1UL << (bar_id & 0x1fU);
                FrameWndShowControlBar(frame, bar,
                    (state.visible_control_bar_mask & bit) != 0, true);
            }
        }
        if (state.saved_focus != nullptr && IsWindow(state.saved_focus)) {
            SetFocus(state.saved_focus);
        }
    }
    frame.idle_update_flags |= 0x0aU;
}

void FrameWndDelayUpdateFrameTitle(MfcFrameWndCompat& frame,
    bool add_to_title) {
    frame.deferred_title_add_to_title = add_to_title;
    frame.idle_update_flags |= 0x01U;
}

void FrameWndOnIdleUpdateCmdUI(MfcFrameWndCompat& frame) {
    const unsigned flags = frame.idle_update_flags;
    if ((flags & 0x01U) != 0) {
        FrameWndOnUpdateFrameTitle(frame, frame.deferred_title_add_to_title);
    }
    if ((flags & 0x02U) != 0) {
        FrameWndOnUpdateFrameMenu(frame, nullptr);
    }
    if ((flags & 0x08U) != 0) {
        FrameWndRecalcLayout(frame, (flags & 0x04U) != 0);
        if (frame.window != nullptr) {
            UpdateWindow(frame.window);
        }
    }
    if (frame.tracking_message_id != frame.last_message_id) {
        FrameWndApplyMessageText(frame, frame.tracking_message_id);
    }
    frame.idle_update_flags = 0;
}

MfcFrameWndCompat* FrameWndGetParentFrameSelf(MfcFrameWndCompat& frame) {
    return &frame;
}

void FrameWndRecalcLayout(MfcFrameWndCompat& frame, bool notify) {
    if (frame.layout_in_progress || frame.window == nullptr) {
        return;
    }
    frame.layout_in_progress = true;
    if ((frame.idle_update_flags & 0x04U) != 0) {
        notify = true;
    }
    frame.idle_update_flags &= ~0x0cU;
    if (notify && frame.active_view != nullptr) {
        ViewOnUpdate(*frame.active_view, nullptr, 0, nullptr);
    }
    if ((CWndGetStyle(frame) & WS_MINIMIZE) == 0) {
        CWndRepositionBars(frame, 0, 0xffff, 0xe900, 2, &frame.layout_rect,
            nullptr, true);
    } else {
        RECT rect{0, 0, 0x7fff, 0x7fff};
        CWndRepositionBars(frame, 0, 0xffff, 0xe900, 1, &rect, &rect,
            false);
        CWndRepositionBars(frame, 0, 0xffff, 0xe900, 2,
            &frame.layout_rect, &rect, true);
        CWndCalcWindowRect(frame, rect, false);
        CWndSetWindowPos(frame, nullptr, 0, 0, static_cast<int>(RectWidth(rect)),
            static_cast<int>(RectHeight(rect)),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    frame.layout_in_progress = false;
}

bool FrameWndNegotiateBorderSpace(MfcFrameWndCompat& frame, UINT border_cmd,
    RECT* rect) {
    switch (border_cmd) {
    case 1:
        if (rect == nullptr) {
            return false;
        }
        CWndRepositionBars(frame, 0, 0xffff, 0xe900, 1, rect, nullptr, true);
        return true;
    case 2:
        return true;
    case 3:
        if (rect != nullptr) {
            if (EqualRect(&frame.layout_rect, rect)) {
                return false;
            }
            frame.layout_rect = *rect;
            return true;
        }
        if (!IsRectEmpty(&frame.layout_rect)) {
            SetRectEmpty(&frame.layout_rect);
            return true;
        }
        return false;
    default:
        AfxTraceOutput("Warning: unknown border-space command %u.\n",
            border_cmd);
        return true;
    }
}

void FrameWndOnSize(MfcFrameWndCompat& frame, UINT type, int cx, int cy) {
    (void)cx;
    (void)cy;
    if (type != SIZE_MINIMIZED) {
        FrameWndRecalcLayout(frame, true);
    }
}

bool FrameWndOnEraseBkgnd(MfcFrameWndCompat& frame, HDC dc) {
    if (frame.active_view != nullptr) {
        return true;
    }
    return frame.window != nullptr &&
        DefWindowProcA(frame.window, WM_ERASEBKGND,
            reinterpret_cast<WPARAM>(dc), 0) != 0;
}

LRESULT FrameWndOnRegisteredMouseWheel(MfcFrameWndCompat& frame, int delta,
    LPARAM point_lparam) {
    const SHORT ctrl = GetKeyState(VK_CONTROL);
    const SHORT shift = GetKeyState(VK_SHIFT);
    const WPARAM wparam = MAKEWPARAM(
        ((ctrl < 0) ? MK_CONTROL : 0) | ((shift < 0) ? MK_SHIFT : 0),
        static_cast<WORD>(delta));
    HWND target = GetFocus();
    if (target == nullptr) {
        return frame.window == nullptr ? 0
            : SendMessageA(frame.window, WM_MOUSEWHEEL, wparam, point_lparam);
    }
    HWND desktop = GetDesktopWindow();
    while (target != nullptr && target != desktop) {
        LRESULT handled = SendMessageA(target, WM_MOUSEWHEEL, wparam,
            point_lparam);
        if (handled != 0) {
            return handled;
        }
        target = GetParent(target);
    }
    return 0;
}

MfcRuntimeClassCompat* GetViewRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CView", static_cast<int>(sizeof(MfcViewCompat)), 0xffff,
        +[]() -> void* {
            auto* view = new MfcViewCompat();
            ConstructView(*view);
            return view;
        },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetViewMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_PAINT, 0, 0, 0, nullptr, 0, nullptr},
        {WM_MOUSEACTIVATE, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

MfcViewCompat& ConstructView(MfcViewCompat& view) {
    ConstructCWnd(view);
    view.runtime_class = GetViewRuntimeClass();
    view.document = nullptr;
    view.active_frame = nullptr;
    view.preview_view = nullptr;
    return view;
}

void DestroyView(MfcViewCompat& view) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->active_window == view.window) {
        thread->active_window = nullptr;
    }
    if (view.document != nullptr) {
        AfxTraceOutput("CView detached from document %p during destruction.\n",
            view.document);
        view.document = nullptr;
    }
    view.active_frame = nullptr;
    view.preview_view = nullptr;
    DestroyCWndCompat(view);
}

MfcViewCompat* DeleteViewScalarDtor(MfcViewCompat* view, unsigned flags) {
    if (view == nullptr) {
        return nullptr;
    }
    DestroyView(*view);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(view);
    }
    return view;
}

bool ViewPreCreateWindow(MfcViewCompat& view, CREATESTRUCTA& create) {
    (void)view;
    if ((create.style & WS_CHILD) == 0) {
        AfxTraceOutput("Warning: CView created without WS_CHILD style.\n");
    }
    if (create.lpszClass == nullptr) {
        if (!AfxDeferRegisterClass(8)) {
            return false;
        }
        create.lpszClass = "AfxFrameOrView42sd";
    }
    if ((create.style & WS_BORDER) != 0) {
        create.dwExStyle |= WS_EX_CLIENTEDGE;
        create.style &= ~static_cast<DWORD>(WS_BORDER);
    }
    return true;
}

int ViewOnCreate(MfcViewCompat& view, CREATESTRUCTA& create) {
    if (view.document != nullptr) {
        AfxTraceOutput("Warning: CView already has a document at OnCreate.\n");
    }

    auto* context = static_cast<MfcCreateContextCompat*>(create.lpCreateParams);
    if (context == nullptr || context->current_doc == nullptr) {
        AfxTraceOutput("Warning: Creating a pane with no CDocument.\n");
        return 0;
    }

    view.document = context->current_doc;
    view.active_frame = context->current_frame;
    return 0;
}

void ViewOnDestroy(MfcViewCompat& view) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->active_window == view.window) {
        thread->active_window = nullptr;
    }
    CWndDefaultAndReleaseControlSite(view);
}

void OnDestroy(MfcViewCompat& view) {
    ViewOnDestroy(view);
}

void ViewPostNcDestroy(MfcViewCompat* view) {
    if (view != nullptr) {
        DeleteViewScalarDtor(view, 1);
    }
}

void ViewCalcWindowRect(MfcViewCompat& view, RECT& rect,
    bool adjust_for_client) {
    if (!adjust_for_client) {
        CWndCalcWindowRect(view, rect, false);
        return;
    }

    const DWORD ex_style = static_cast<DWORD>(CWndGetExStyle(view));
    AdjustWindowRectEx(&rect, 0, FALSE, ex_style);

    const DWORD style = static_cast<DWORD>(CWndGetStyle(view));
    const int border_adjust = (style & WS_BORDER) != 0 ? -1 : 0;
    if ((style & WS_VSCROLL) != 0) {
        rect.right += GetSystemMetrics(SM_CXVSCROLL) + border_adjust;
    }
    if ((style & WS_HSCROLL) != 0) {
        rect.bottom += GetSystemMetrics(SM_CYHSCROLL) + border_adjust;
    }
}

bool ViewOnCmdMsg(MfcViewCompat& view, UINT id, int code, void* extra,
    MfcCommandHandlerInfoCompat* handler_info) {
    MfcCommandTargetCompat local_target{};
    ConstructCmdTarget(local_target);
    local_target.message_map = GetViewMessageMap();
    if (CmdTargetOnCmdMsg(local_target, id, code, extra, handler_info)) {
        return true;
    }

    if (view.document == nullptr) {
        return false;
    }
    auto* document_target =
        reinterpret_cast<MfcCommandTargetCompat*>(view.document);
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    HWND previous_active = nullptr;
    if (thread != nullptr) {
        previous_active = thread->active_window;
        thread->active_window = view.window;
    }
    const bool handled = CmdTargetOnCmdMsg(*document_target, id, code, extra,
        handler_info);
    if (thread != nullptr) {
        thread->active_window = previous_active;
    }
    return handled;
}

void ViewOnPaint(MfcViewCompat& view) {
    MfcWindowDCCompat paint_dc;
    ConstructPaintDC(paint_dc, view.window);
    ViewOnPrepareDC(view, paint_dc, nullptr);
    ViewOnDrawDefault(view, paint_dc);
    DestroyPaintDC(paint_dc);
}

void ViewOnInitialUpdate(MfcViewCompat& view) {
    ViewOnUpdate(view, nullptr, 0, nullptr);
}

void ViewOnUpdate(MfcViewCompat& view, MfcViewCompat* sender, LPARAM hint,
    void* hint_object) {
    (void)hint;
    (void)hint_object;
    if (sender == &view) {
        AfxTraceOutput("Warning: CView::OnUpdate called with itself as sender.\n");
        return;
    }
    if (view.window != nullptr && IsWindow(view.window)) {
        InvalidateRect(view.window, nullptr, TRUE);
    }
}

void ViewOnPrint(MfcViewCompat& view, MfcCDCCompat& dc, void* print_info) {
    (void)print_info;
    AfxAssertValidObject(&dc, "viewcore.cpp", 0xcb);
    ViewOnDrawDefault(view, dc);
}

void ViewOnDrawDefault(MfcViewCompat& view, MfcCDCCompat& dc) {
    (void)view;
    (void)dc;
}

bool ViewIsSelectedDefault(const MfcViewCompat& view,
    const MfcObjectCompat* item) {
    (void)view;
    AfxAssertValidObject(item, "viewcore.cpp", 0xda);
    return false;
}

DWORD ViewOnDragEnterDefault(MfcViewCompat& view, void* data_object,
    DWORD key_state, POINT point) {
    (void)view;
    (void)data_object;
    (void)key_state;
    (void)point;
    return kMfcDropEffectNone;
}

DWORD ViewOnDragOverDefault(MfcViewCompat& view, void* data_object,
    DWORD key_state, POINT point) {
    (void)view;
    (void)data_object;
    (void)key_state;
    (void)point;
    return kMfcDropEffectNone;
}

bool ViewOnDropDefault(MfcViewCompat& view, void* data_object,
    DWORD drop_effect, POINT point) {
    (void)view;
    (void)data_object;
    (void)drop_effect;
    (void)point;
    return false;
}

DWORD ViewOnDropExDefault(MfcViewCompat& view, void* data_object,
    DWORD drop_default, DWORD drop_list, POINT point) {
    (void)view;
    (void)data_object;
    (void)drop_default;
    (void)drop_list;
    (void)point;
    return kMfcDropEffectUseDefault;
}

void ViewOnDragLeaveDefault(MfcViewCompat& view) {
    (void)view;
}

bool ViewOnScrollDefault(MfcViewCompat& view, UINT scroll_code,
    UINT position, bool do_scroll) {
    (void)view;
    (void)scroll_code;
    (void)position;
    (void)do_scroll;
    return false;
}

bool ViewOnScrollByDefault(MfcViewCompat& view, SIZE scroll_size,
    bool do_scroll) {
    (void)view;
    (void)scroll_size;
    (void)do_scroll;
    return false;
}

DWORD ViewOnDragScrollDefault(MfcViewCompat& view, DWORD key_state,
    POINT point) {
    (void)view;
    (void)key_state;
    (void)point;
    return kMfcDropEffectScroll;
}

void ViewOnPrepareDC(MfcViewCompat& view, MfcCDCCompat& dc, void* print_info) {
    (void)view;
    (void)print_info;
    AfxAssertValidObject(&dc, "viewcore.cpp", 0x19f);
}

void ViewOnActivateView(MfcViewCompat& view, bool active,
    MfcViewCompat* active_view, MfcViewCompat* inactive_view) {
    (void)inactive_view;
    if (!active) {
        return;
    }
    if (active_view != nullptr && active_view != &view) {
        AfxTraceOutput("Warning: CView activated with mismatched active view.\n");
    }
    if (CWndIsTopParentActive(view)) {
        CWndSetFocus(view);
    }
}

void ViewOnActivateFrameDefault(MfcViewCompat& view, UINT state, void* frame) {
    (void)view;
    (void)state;
    (void)frame;
}

int ViewOnMouseActivate(MfcViewCompat& view, MfcCWndCompat* top_level,
    UINT hit_test, UINT message) {
    const int activate = OnMouseActivate(view, top_level, hit_test, message);
    if (activate != MA_NOACTIVATE && activate != MA_NOACTIVATEANDEAT) {
        MfcFrameWndCompat* frame = CWndGetParentFrameCompat(view);
        if (frame != nullptr) {
            MfcViewCompat* active_view = FrameWndGetActiveView(*frame);
            HWND focus = GetFocus();
            if (active_view == &view && view.window != nullptr &&
                focus != view.window && IsChild(view.window, focus) == 0) {
                ViewOnActivateView(view, true, &view, &view);
            } else {
                FrameWndSetActiveView(*frame, &view, true);
            }
        }
    }
    return activate;
}

MfcCWndCompat* ViewGetParentSplitter(const MfcViewCompat& view,
    bool check_nested) {
    (void)check_nested;
    if (view.window == nullptr || !IsWindow(view.window)) {
        return nullptr;
    }
    HWND parent = GetParent(view.window);
    return parent == nullptr ? nullptr : CWndFromHandle(parent);
}

MfcCWndCompat* ViewGetSplitScrollSibling(MfcViewCompat& view, bool vertical) {
    const DWORD style = static_cast<DWORD>(CWndGetStyle(view));
    const DWORD scroll_style = vertical ? WS_VSCROLL : WS_HSCROLL;
    if ((style & scroll_style) != 0) {
        return nullptr;
    }

    MfcCWndCompat* splitter = ViewGetParentSplitter(view, true);
    if (splitter == nullptr) {
        return nullptr;
    }
    const UINT id = static_cast<UINT>(CWndGetDlgCtrlID(view)) & 0xffffU;
    if (id < 0xe900U || id > 0xe9ffU) {
        return nullptr;
    }

    const int sibling_id = vertical
        ? static_cast<int>(((id - 0xe900U) >> 4) + 0xea10U)
        : static_cast<int>(((id - 0xe900U) & 0x0fU) + 0xea00U);
    return CWndGetDlgItem(*splitter, sibling_id);
}

static MfcSplitterWndCompat* ViewGetParentSplitterCompat(MfcViewCompat& view,
    bool check_nested) {
    MfcCWndCompat* parent = ViewGetParentSplitter(view, check_nested);
    if (parent == nullptr ||
        !ObjectIsKindOfRuntimeClass(parent, GetSplitterRuntimeClass())) {
        return nullptr;
    }
    return static_cast<MfcSplitterWndCompat*>(parent);
}

void ViewOnUpdateSplitCmd(MfcViewCompat& view, MfcCmdUICompat& cmd_ui) {
    MfcSplitterWndCompat* splitter = ViewGetParentSplitterCompat(view, false);
    CmdUIEnable(cmd_ui, splitter != nullptr && !splitter->tracking);
}

bool ViewSplitCommand(MfcViewCompat& view) {
    MfcSplitterWndCompat* splitter = ViewGetParentSplitterCompat(view, false);
    if (splitter == nullptr) {
        return false;
    }
    if (splitter->tracking) {
        AfxTraceOutput("Warning: split command while splitter is tracking.\n");
    }
    SplitterDoKeyboardSplit(*splitter);
    return true;
}

void ViewOnUpdateNextPaneCmd(MfcViewCompat& view, MfcCmdUICompat& cmd_ui) {
    const bool valid_id = cmd_ui.id == kMfcIdNextPane ||
        cmd_ui.id == kMfcIdPrevPane;
    MfcSplitterWndCompat* splitter = ViewGetParentSplitterCompat(view, false);
    CmdUIEnable(cmd_ui, valid_id && splitter != nullptr &&
        SplitterCanActivateNext(*splitter));
}

bool ViewOnNextPaneCmd(MfcViewCompat& view, UINT command) {
    MfcSplitterWndCompat* splitter = ViewGetParentSplitterCompat(view, false);
    if (splitter == nullptr) {
        return false;
    }
    if (command != kMfcIdNextPane && command != kMfcIdPrevPane) {
        AfxTraceOutput("Warning: invalid CView next-pane command 0x%04x.\n",
            command);
        return false;
    }
    SplitterActivateNext(*splitter, command == kMfcIdPrevPane);
    return true;
}

bool ViewPreparePrintingDefault(MfcViewCompat& view, void* print_info) {
    (void)view;
    (void)print_info;
    return true;
}

void ViewOnBeginPrintingDefault(MfcViewCompat& view, MfcCDCCompat& dc,
    void* print_info) {
    (void)view;
    (void)print_info;
    AfxAssertValidObject(&dc, "viewcore.cpp", 0x1b2);
}

void ViewOnEndPrintingDefault(MfcViewCompat& view, MfcCDCCompat& dc,
    void* print_info) {
    (void)view;
    (void)print_info;
    AfxAssertValidObject(&dc, "viewcore.cpp", 0x1ba);
}

void ViewOnEndPrintPreview(MfcViewCompat& view, MfcCDCCompat& dc,
    void* print_info, POINT point, void* preview_view) {
    (void)print_info;
    (void)point;
    AfxAssertValidObject(&dc, "viewcore.cpp", 0x1c5);
    view.preview_view = preview_view;
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->main_window != nullptr &&
        IsWindow(thread->main_window)) {
        EnableWindow(thread->main_window, TRUE);
        UpdateWindow(thread->main_window);
    }
    if (view.window != nullptr && IsWindow(view.window)) {
        SetFocus(view.window);
    }
    view.preview_view = nullptr;
}

void ViewDump(const MfcViewCompat& view) {
    CWndDump(view);
    if (view.document == nullptr) {
        AfxTraceOutput(" with no document\n");
    } else {
        AfxTraceOutput(" with document %p\n", view.document);
    }
}

void ViewAssertValid(const MfcViewCompat& view) {
    CWndAssertValid(view);
}

MfcRuntimeClassCompat* GetCtrlViewRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CCtrlView", static_cast<int>(sizeof(MfcCtrlViewCompat)), 0xffff,
        nullptr, GetViewRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetCtrlViewMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{GetViewMessageMap(), entries};
    return &map;
}

MfcCtrlViewCompat& ConstructCtrlView(MfcCtrlViewCompat& view,
    const char* class_name, DWORD default_style) {
    ConstructView(view);
    view.runtime_class = GetCtrlViewRuntimeClass();
    view.control_class_name = class_name == nullptr ? "" : class_name;
    view.default_style = default_style;
    return view;
}

void DestroyCtrlView(MfcCtrlViewCompat& view) {
    view.control_class_name.clear();
    view.default_style = 0;
    DestroyView(view);
}

MfcCtrlViewCompat* DeleteCtrlViewScalarDtor(MfcCtrlViewCompat* view,
    unsigned flags) {
    if (view == nullptr) {
        return nullptr;
    }
    DestroyCtrlView(*view);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(view);
    }
    return view;
}

bool CtrlViewPreCreateWindow(MfcCtrlViewCompat& view, CREATESTRUCTA& create) {
    if (create.lpszClass != nullptr) {
        AfxTraceOutput("Warning: CCtrlView overriding supplied class name.\n");
    }
    create.lpszClass = view.control_class_name.empty()
        ? nullptr : view.control_class_name.c_str();
    if (!AfxDeferRegisterClass(0x10)) {
        return false;
    }
    AfxDeferRegisterClass(0x3c000);
    if ((create.style & (WS_CHILD | WS_VISIBLE)) == (WS_CHILD | WS_VISIBLE)) {
        create.style = (create.style & ~WS_BORDER) | view.default_style;
    }
    return ViewPreCreateWindow(view, create);
}

void CtrlViewOnDrawAssert(MfcCtrlViewCompat& view, MfcCDCCompat& dc) {
    (void)view;
    (void)dc;
    AfxTraceOutput("Error: CCtrlView::OnDraw should not be called.\n");
}

void CtrlViewOnDestroyDefault(MfcCtrlViewCompat& view) {
    if (view.window != nullptr && IsWindow(view.window)) {
        DefWindowProcA(view.window, WM_DESTROY, 0, 0);
    }
}

void CtrlViewDump(const MfcCtrlViewCompat& view) {
    ViewDump(view);
    AfxTraceOutput(" class name = %s\n", view.control_class_name.c_str());
    AfxTraceOutput(" default style = 0x%08lx\n", view.default_style);
}

void CtrlViewAssertValid(const MfcCtrlViewCompat& view) {
    CWndAssertValid(view);
    if (view.control_class_name.empty()) {
        AfxTraceOutput("Warning: CCtrlView has no control class name.\n");
    }
}

MfcRuntimeClassCompat* GetScrollViewRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CScrollView", static_cast<int>(sizeof(MfcScrollViewCompat)), 0xffff,
        +[]() -> void* {
            auto* view = new MfcScrollViewCompat();
            ConstructScrollView(*view);
            return view;
        },
        GetViewRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetScrollViewMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_SIZE, 0, 0, 0, nullptr, 0, nullptr},
        {WM_HSCROLL, 0, 0, 0, nullptr, 0, nullptr},
        {WM_VSCROLL, 0, 0, 0, nullptr, 0, nullptr},
        {WM_MOUSEWHEEL, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{GetViewMessageMap(), entries};
    return &map;
}

UINT GetMouseWheelScrollLines() {
    static bool cached = false;
    static UINT scroll_lines = 3;
    if (cached) {
        return scroll_lines;
    }
    cached = true;

    UINT registered = RegisterWindowMessageA("MSH_SCROLL_LINES_MSG");
    HWND mouse_z = FindWindowA("MouseZ", "Magellan MSWHEEL");
    if (registered != 0 && mouse_z != nullptr) {
        LRESULT value = SendMessageA(mouse_z, registered, 0, 0);
        if (value > 0) {
            scroll_lines = static_cast<UINT>(value);
            return scroll_lines;
        }
    }

    UINT value = scroll_lines;
    if (SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &value, 0)) {
        scroll_lines = value;
        return scroll_lines;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0,
        KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        char buffer[128]{};
        DWORD type = 0;
        DWORD bytes = sizeof(buffer);
        if (RegQueryValueExA(key, "WheelScrollLines", nullptr, &type,
            reinterpret_cast<BYTE*>(buffer), &bytes) == ERROR_SUCCESS) {
            scroll_lines = static_cast<UINT>(std::strtoul(buffer, nullptr, 10));
        }
        RegCloseKey(key);
    }
    return scroll_lines;
}

MfcScrollViewCompat& ConstructScrollView(MfcScrollViewCompat& view) {
    ConstructView(view);
    view.runtime_class = GetScrollViewRuntimeClass();
    view.map_mode = 0;
    view.total_log = SIZE{};
    view.total_dev = SIZE{};
    view.page_dev = SIZE{};
    view.line_dev = SIZE{};
    view.center = false;
    view.inside_update = false;
    return view;
}

void DestroyScrollView(MfcScrollViewCompat& view) {
    view.map_mode = 0;
    view.total_log = SIZE{};
    view.total_dev = SIZE{};
    view.page_dev = SIZE{};
    view.line_dev = SIZE{};
    view.center = false;
    view.inside_update = false;
    DestroyView(view);
}

MfcScrollViewCompat* DeleteScrollViewScalarDtor(MfcScrollViewCompat* view,
    unsigned flags) {
    if (view == nullptr) {
        return nullptr;
    }
    DestroyScrollView(*view);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(view);
    }
    return view;
}

void ScrollViewOnPrepareDC(MfcScrollViewCompat& view, MfcCDCCompat& dc,
    void* print_info) {
    AfxAssertValidObject(&dc, "viewscrl.cpp", 0x7f);
    if (view.map_mode == 0) {
        AfxTraceOutput("Error: must call SetScrollSizes before painting scroll view.\n");
        return;
    }
    if (view.map_mode == -1) {
        CDCSetMapMode(dc, MM_ANISOTROPIC);
        CDCSetWindowExt(dc, view.total_log.cx, view.total_log.cy);
        CDCSetViewportExt(dc, view.total_dev.cx, view.total_dev.cy);
        if (view.total_dev.cx == 0 || view.total_dev.cy == 0) {
            AfxTraceOutput("Warning: CScrollView scaled to nothing.\n");
        }
    } else {
        CDCSetMapMode(dc, view.map_mode);
    }
    POINT viewport_org{};
    if (!CDCIsPrinting(dc)) {
        POINT position = ScrollViewGetDeviceScrollPosition(view);
        viewport_org.x = -position.x;
        viewport_org.y = -position.y;
    }
    CDCSetViewportOrg(dc, viewport_org.x, viewport_org.y);
    ViewOnPrepareDC(view, dc, print_info);
}

void ScrollViewSetScaleToFitSize(MfcScrollViewCompat& view, SIZE total_log) {
    view.map_mode = -1;
    view.total_log = total_log;
    RECT client{};
    if (view.window != nullptr && IsWindow(view.window)) {
        GetClientRect(view.window, &client);
    }
    view.total_dev.cx = client.right - client.left;
    view.total_dev.cy = client.bottom - client.top;
    if (view.window != nullptr && IsWindow(view.window)) {
        ShowScrollBar(view.window, SB_BOTH, FALSE);
        ScrollViewUpdateBars(view);
        InvalidateRect(view.window, nullptr, TRUE);
    }
}

static POINT ScrollViewConvertWithWindowDC(int map_mode, POINT point,
    bool logical_to_device) {
    if (map_mode == MM_TEXT || map_mode < 1) {
        return point;
    }
    MfcWindowDCCompat dc{};
    ConstructWindowDC(dc, nullptr);
    CDCSetMapMode(dc, map_mode);
    if (logical_to_device) {
        CDCLPtoDPPointsInline(dc, &point, 1);
    } else {
        CDCDPtoLPPointsInline(dc, &point, 1);
    }
    DestroyWindowDC(dc);
    return point;
}

static SIZE ScrollViewLogicalSizeToDevice(int map_mode, SIZE size) {
    POINT point{size.cx, size.cy};
    point = ScrollViewConvertWithWindowDC(map_mode, point, true);
    return SIZE{point.x, point.y};
}

void ScrollViewSetScrollSizes(MfcScrollViewCompat& view, int map_mode,
    SIZE total_log, SIZE page_size, SIZE line_size) {
    if (map_mode < 1 || map_mode == MM_ISOTROPIC || map_mode == MM_ANISOTROPIC) {
        AfxTraceOutput("Warning: unsupported CScrollView map mode %d.\n",
            map_mode);
        return;
    }
    const int old_mode = view.map_mode;
    view.map_mode = map_mode;
    view.total_log = total_log;
    view.total_dev = ScrollViewLogicalSizeToDevice(view.map_mode, total_log);
    view.page_dev = ScrollViewLogicalSizeToDevice(view.map_mode, page_size);
    view.line_dev = ScrollViewLogicalSizeToDevice(view.map_mode, line_size);
    view.total_dev.cy = std::abs(view.total_dev.cy);
    view.page_dev.cy = std::abs(view.page_dev.cy);
    view.line_dev.cy = std::abs(view.line_dev.cy);
    if (view.page_dev.cx == 0) {
        view.page_dev.cx = view.total_dev.cx / 10;
    }
    if (view.page_dev.cy == 0) {
        view.page_dev.cy = view.total_dev.cy / 10;
    }
    if (view.line_dev.cx == 0) {
        view.line_dev.cx = view.page_dev.cx / 10;
    }
    if (view.line_dev.cy == 0) {
        view.line_dev.cy = view.page_dev.cy / 10;
    }
    if (view.window != nullptr && IsWindow(view.window)) {
        ScrollViewUpdateBars(view);
        if (old_mode != view.map_mode) {
            InvalidateRect(view.window, nullptr, TRUE);
        }
    }
}

POINT ScrollViewGetScrollPosition(MfcScrollViewCompat& view) {
    if (view.map_mode == -1) {
        return POINT{0, 0};
    }
    POINT position = ScrollViewGetDeviceScrollPosition(view);
    position = ScrollViewConvertWithWindowDC(view.map_mode, position, false);
    return position;
}

void ScrollViewScrollToPosition(MfcScrollViewCompat& view, POINT point) {
    if (view.map_mode < 1) {
        return;
    }
    point = ScrollViewConvertWithWindowDC(view.map_mode, point, true);
    const LONG max_x = static_cast<LONG>(CWndGetScrollLimitCompat(view, SB_HORZ));
    const LONG max_y = static_cast<LONG>(CWndGetScrollLimitCompat(view, SB_VERT));
    point.x = std::max<LONG>(0, std::min<LONG>(point.x, max_x));
    point.y = std::max<LONG>(0, std::min<LONG>(point.y, max_y));
    ScrollViewScrollToDevicePosition(view, point);
}

POINT ScrollViewGetDeviceScrollPosition(MfcScrollViewCompat& view) {
    POINT position{};
    if (view.window != nullptr && IsWindow(view.window)) {
        position.x = GetScrollPos(view.window, SB_HORZ);
        position.y = GetScrollPos(view.window, SB_VERT);
    }
    if (view.center && view.window != nullptr && IsWindow(view.window)) {
        RECT client{};
        GetClientRect(view.window, &client);
        const int client_cx = client.right - client.left;
        const int client_cy = client.bottom - client.top;
        if (view.total_dev.cx < client_cx) {
            position.x = -((client_cx - view.total_dev.cx) / 2);
        }
        if (view.total_dev.cy < client_cy) {
            position.y = -((client_cy - view.total_dev.cy) / 2);
        }
    }
    return position;
}

void ScrollViewGetDeviceScrollSizes(const MfcScrollViewCompat& view,
    int& map_mode, SIZE& total, SIZE& page, SIZE& line) {
    map_mode = view.map_mode;
    total = view.total_dev;
    page = view.page_dev;
    line = view.line_dev;
}

void ScrollViewScrollToDevicePosition(MfcScrollViewCompat& view, POINT point) {
    if (view.window == nullptr || !IsWindow(view.window)) {
        return;
    }
    if (point.x < 0) {
        point.x = 0;
    }
    if (point.y < 0) {
        point.y = 0;
    }
    const int old_x = GetScrollPos(view.window, SB_HORZ);
    const int old_y = GetScrollPos(view.window, SB_VERT);
    SetScrollPos(view.window, SB_HORZ, point.x, TRUE);
    SetScrollPos(view.window, SB_VERT, point.y, TRUE);
    CWndScrollWindowCompat(view, old_x - point.x, old_y - point.y, nullptr,
        nullptr);
}

void ScrollViewFillOutsideRect(MfcScrollViewCompat& view, MfcCDCCompat& dc,
    HBRUSH brush) {
    AfxAssertValidObject(&view, "viewscrl.cpp", 0x176);
    AfxAssertValidObject(&dc, "viewscrl.cpp", 0x177);
    if (view.window == nullptr || brush == nullptr || dc.output_dc == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(view.window, &client);
    RECT right{view.total_dev.cx, 0, client.right, client.bottom};
    RECT bottom{0, view.total_dev.cy, client.right, client.bottom};
    if (right.left < right.right) {
        FillRect(dc.output_dc, &right, brush);
    }
    if (bottom.top < bottom.bottom) {
        FillRect(dc.output_dc, &bottom, brush);
    }
}

void ScrollViewResizeParentToFit(MfcScrollViewCompat& view, bool shrink_only) {
    if (view.window == nullptr || !IsWindow(view.window)) {
        return;
    }
    HWND parent = GetParent(view.window);
    if (parent == nullptr) {
        return;
    }
    RECT current{};
    GetWindowRect(parent, &current);
    const int current_cx = current.right - current.left;
    const int current_cy = current.bottom - current.top;
    int target_cx = view.total_dev.cx;
    int target_cy = view.total_dev.cy;
    if (shrink_only) {
        target_cx = std::min(target_cx, current_cx);
        target_cy = std::min(target_cy, current_cy);
    }
    if (target_cx > 0 && target_cy > 0) {
        SetWindowPos(parent, nullptr, 0, 0, target_cx, target_cy,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void ScrollViewOnSize(MfcScrollViewCompat& view, UINT type, int cx, int cy) {
    (void)type;
    if (view.map_mode == -1) {
        SIZE size{cx, cy};
        ScrollViewSetScaleToFitSize(view, size);
    } else {
        ScrollViewUpdateBars(view);
    }
}

void ScrollViewCenterOnPoint(MfcScrollViewCompat& view, POINT point) {
    if (view.window == nullptr || !IsWindow(view.window)) {
        return;
    }
    RECT client{};
    GetClientRect(view.window, &client);
    POINT target{point.x - (client.right - client.left) / 2,
        point.y - (client.bottom - client.top) / 2};
    const LONG max_x = static_cast<LONG>(CWndGetScrollLimitCompat(view, SB_HORZ));
    const LONG max_y = static_cast<LONG>(CWndGetScrollLimitCompat(view, SB_VERT));
    target.x = std::max<LONG>(0, std::min<LONG>(target.x, max_x));
    target.y = std::max<LONG>(0, std::min<LONG>(target.y, max_y));
    SetScrollPos(view.window, SB_HORZ, target.x, TRUE);
    SetScrollPos(view.window, SB_VERT, target.y, TRUE);
}

MfcCWndCompat* ScrollViewGetScrollBarCtrl(MfcScrollViewCompat& view,
    bool vertical) {
    return ViewGetSplitScrollSibling(view, vertical);
}

SIZE ScrollViewGetScrollBarSizes(MfcScrollViewCompat& view) {
    SIZE sizes{};
    const DWORD style = static_cast<DWORD>(CWndGetStyle(view));
    if (ScrollViewGetScrollBarCtrl(view, true) == nullptr) {
        sizes.cx = GetSystemMetrics(SM_CXVSCROLL);
        if ((style & WS_BORDER) != 0) {
            --sizes.cx;
        }
    }
    if (ScrollViewGetScrollBarCtrl(view, false) == nullptr) {
        sizes.cy = GetSystemMetrics(SM_CYHSCROLL);
        if ((style & WS_BORDER) != 0) {
            --sizes.cy;
        }
    }
    return sizes;
}

bool ScrollViewGetTrueClientSize(MfcScrollViewCompat& view, SIZE& size,
    SIZE& scrollbars) {
    RECT client{};
    if (view.window != nullptr && IsWindow(view.window)) {
        GetClientRect(view.window, &client);
    }
    size.cx = client.right - client.left;
    size.cy = client.bottom - client.top;
    scrollbars = ScrollViewGetScrollBarSizes(view);
    const DWORD style = static_cast<DWORD>(CWndGetStyle(view));
    if ((style & WS_VSCROLL) != 0) {
        size.cx += scrollbars.cx;
    }
    if ((style & WS_HSCROLL) != 0) {
        size.cy += scrollbars.cy;
    }
    return scrollbars.cx < size.cx && scrollbars.cy < size.cy;
}

void ScrollViewGetScrollBarState(MfcScrollViewCompat& view, SIZE inside,
    SIZE& need_bars, SIZE& ranges, POINT& position, bool inside_client) {
    SIZE bars = ScrollViewGetScrollBarSizes(view);
    position = ScrollViewGetDeviceScrollPosition(view);
    ranges = view.total_dev;
    need_bars.cx = view.total_dev.cx > inside.cx ? 1 : 0;
    need_bars.cy = view.total_dev.cy > inside.cy ? 1 : 0;
    if (inside_client && need_bars.cy && !need_bars.cx &&
        view.total_dev.cx > inside.cx - bars.cx) {
        need_bars.cx = 1;
    }
    if (inside_client && need_bars.cx && !need_bars.cy &&
        view.total_dev.cy > inside.cy - bars.cy) {
        need_bars.cy = 1;
    }
    if (!need_bars.cx) {
        position.x = 0;
    }
    if (!need_bars.cy) {
        position.y = 0;
    }
}

void ScrollViewUpdateBars(MfcScrollViewCompat& view) {
    if (view.inside_update) {
        return;
    }
    view.inside_update = true;
    SIZE client{};
    SIZE bars{};
    if (!ScrollViewGetTrueClientSize(view, client, bars)) {
        if (view.window != nullptr && IsWindow(view.window)) {
            ShowScrollBar(view.window, SB_BOTH, FALSE);
        }
        view.inside_update = false;
        return;
    }
    SIZE need{};
    SIZE ranges{};
    POINT position{};
    ScrollViewGetScrollBarState(view, client, need, ranges, position, true);
    ScrollViewScrollToDevicePosition(view, position);
    if (view.window != nullptr && IsWindow(view.window)) {
        ShowScrollBar(view.window, SB_HORZ, need.cx != 0);
        ShowScrollBar(view.window, SB_VERT, need.cy != 0);
        if (need.cx != 0) {
            SCROLLINFO info{sizeof(info), SIF_PAGE | SIF_RANGE | SIF_POS};
            info.nMin = 0;
            info.nMax = static_cast<int>(
                std::max<LONG>(0, view.total_dev.cx - 1));
            info.nPage = static_cast<UINT>(std::max<LONG>(0, client.cx));
            info.nPos = position.x;
            CWndSetScrollInfoCompat(view, SB_HORZ, info, TRUE);
        }
        if (need.cy != 0) {
            SCROLLINFO info{sizeof(info), SIF_PAGE | SIF_RANGE | SIF_POS};
            info.nMin = 0;
            info.nMax = static_cast<int>(
                std::max<LONG>(0, view.total_dev.cy - 1));
            info.nPage = static_cast<UINT>(std::max<LONG>(0, client.cy));
            info.nPos = position.y;
            CWndSetScrollInfoCompat(view, SB_VERT, info, TRUE);
        }
    }
    view.inside_update = false;
}

void ScrollViewCalcWindowRect(MfcScrollViewCompat& view, RECT& rect,
    bool adjust_for_client) {
    if (!adjust_for_client) {
        DWORD ex_style = static_cast<DWORD>(CWndGetExStyle(view));
        ex_style &= ~WS_EX_CLIENTEDGE;
        AdjustWindowRectEx(&rect, static_cast<DWORD>(CWndGetStyle(view)),
            FALSE, ex_style);
        return;
    }
    ViewCalcWindowRect(view, rect, true);
    if (view.map_mode != -1) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        SIZE bars = ScrollViewGetScrollBarSizes(view);
        if (view.total_dev.cy > height) {
            rect.right += bars.cx;
        }
        if (view.total_dev.cx > width) {
            rect.bottom += bars.cy;
        }
    }
}

void ScrollViewOnHScroll(MfcScrollViewCompat& view, UINT code, UINT position,
    MfcCWndCompat* scroll_bar) {
    if (scroll_bar != nullptr && scroll_bar != ScrollViewGetScrollBarCtrl(view,
        false)) {
        return;
    }
    ScrollViewOnScroll(view, code, position, true);
}

void ScrollViewOnVScroll(MfcScrollViewCompat& view, UINT code, UINT position,
    MfcCWndCompat* scroll_bar) {
    if (scroll_bar != nullptr && scroll_bar != ScrollViewGetScrollBarCtrl(view,
        true)) {
        return;
    }
    ScrollViewOnScroll(view, code << 8, position, true);
}

bool ScrollViewOnMouseWheel(MfcScrollViewCompat& view, UINT flags,
    short delta, POINT point) {
    if ((flags & (MK_SHIFT | MK_CONTROL)) != 0) {
        return false;
    }
    if (ViewGetParentSplitter(view, true) != nullptr) {
        return false;
    }
    return ScrollViewDoMouseWheel(view, flags, delta, point);
}

bool ScrollViewDoMouseWheel(MfcScrollViewCompat& view, UINT flags,
    short delta, POINT point) {
    (void)flags;
    (void)point;
    const UINT lines = GetMouseWheelScrollLines();
    int scroll_delta = 0;
    if (lines == static_cast<UINT>(-1)) {
        scroll_delta = delta > 0 ? -view.page_dev.cy : view.page_dev.cy;
    } else {
        scroll_delta = -MulDiv(delta, static_cast<int>(lines), WHEEL_DELTA) *
            view.line_dev.cy;
    }
    if (scroll_delta == 0) {
        scroll_delta = delta > 0 ? -view.line_dev.cy : view.line_dev.cy;
    }
    SIZE scroll{0, scroll_delta};
    if ((CWndGetStyle(view) & WS_VSCROLL) == 0) {
        scroll.cx = scroll_delta;
        scroll.cy = 0;
    }
    const bool scrolled = ScrollViewOnScrollBy(view, scroll, true);
    if (scrolled && view.window != nullptr && IsWindow(view.window)) {
        UpdateWindow(view.window);
    }
    return scrolled;
}

bool ScrollViewOnScroll(MfcScrollViewCompat& view, UINT scroll_code,
    UINT position, bool do_scroll) {
    POINT current = ScrollViewGetDeviceScrollPosition(view);
    POINT target = current;
    switch (scroll_code & 0xffU) {
    case SB_LINELEFT:
        target.x -= view.line_dev.cx;
        break;
    case SB_LINERIGHT:
        target.x += view.line_dev.cx;
        break;
    case SB_PAGELEFT:
        target.x -= view.page_dev.cx;
        break;
    case SB_PAGERIGHT:
        target.x += view.page_dev.cx;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        target.x = static_cast<int>(position);
        break;
    case SB_LEFT:
        target.x = 0;
        break;
    case SB_RIGHT:
        target.x = 0x7fffffff;
        break;
    default:
        break;
    }
    switch ((scroll_code >> 8) & 0xffU) {
    case SB_LINEUP:
        target.y -= view.line_dev.cy;
        break;
    case SB_LINEDOWN:
        target.y += view.line_dev.cy;
        break;
    case SB_PAGEUP:
        target.y -= view.page_dev.cy;
        break;
    case SB_PAGEDOWN:
        target.y += view.page_dev.cy;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        target.y = static_cast<int>(position);
        break;
    case SB_TOP:
        target.y = 0;
        break;
    case SB_BOTTOM:
        target.y = 0x7fffffff;
        break;
    default:
        break;
    }
    SIZE delta{target.x - current.x, target.y - current.y};
    return ScrollViewOnScrollBy(view, delta, do_scroll);
}

bool ScrollViewOnScrollBy(MfcScrollViewCompat& view, SIZE scroll_size,
    bool do_scroll) {
    if (view.window == nullptr || !IsWindow(view.window)) {
        return false;
    }
    POINT current = ScrollViewGetDeviceScrollPosition(view);
    POINT target{current.x + scroll_size.cx, current.y + scroll_size.cy};
    const LONG max_x = static_cast<LONG>(CWndGetScrollLimitCompat(view, SB_HORZ));
    const LONG max_y = static_cast<LONG>(CWndGetScrollLimitCompat(view, SB_VERT));
    target.x = std::max<LONG>(0, std::min<LONG>(target.x, max_x));
    target.y = std::max<LONG>(0, std::min<LONG>(target.y, max_y));
    if (target.x == current.x && target.y == current.y) {
        return false;
    }
    if (do_scroll) {
        ScrollViewScrollToDevicePosition(view, target);
    }
    return true;
}

void ScrollViewDump(const MfcScrollViewCompat& view) {
    ViewDump(view);
    AfxTraceOutput("m_totalLog = (%ld,%ld)\n", view.total_log.cx,
        view.total_log.cy);
    AfxTraceOutput("m_totalDev = (%ld,%ld)\n", view.total_dev.cx,
        view.total_dev.cy);
    AfxTraceOutput("m_pageDev = (%ld,%ld)\n", view.page_dev.cx,
        view.page_dev.cy);
    AfxTraceOutput("m_lineDev = (%ld,%ld)\n", view.line_dev.cx,
        view.line_dev.cy);
    AfxTraceOutput("m_bCenter = %d\n", view.center ? 1 : 0);
    AfxTraceOutput("m_bInsideUpdate = %d\n", view.inside_update ? 1 : 0);
    AfxTraceOutput("m_nMapMode = %d\n", view.map_mode);
}

void ScrollViewAssertValid(const MfcScrollViewCompat& view) {
    ViewAssertValid(view);
    if (view.map_mode == MM_ISOTROPIC || view.map_mode == MM_ANISOTROPIC) {
        AfxTraceOutput("Warning: CScrollView has unsupported map mode %d.\n",
            view.map_mode);
    }
}

MfcRuntimeClassCompat* GetSplitterRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CSplitterWnd", static_cast<int>(sizeof(MfcSplitterWndCompat)), 0xffff,
        +[]() -> void* {
            auto* splitter = new MfcSplitterWndCompat();
            ConstructSplitterWnd(*splitter);
            return splitter;
        },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetSplitterMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NCCREATE, 0, 0, 0, nullptr, 0, nullptr},
        {WM_SIZE, 0, 0, 0, nullptr, 0, nullptr},
        {WM_HSCROLL, 0, 0, 0, nullptr, 0, nullptr},
        {WM_VSCROLL, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

MfcSplitterWndCompat& ConstructSplitterWnd(MfcSplitterWndCompat& splitter) {
    ConstructCWnd(splitter);
    splitter.runtime_class = GetSplitterRuntimeClass();
    splitter.dynamic_view_class = nullptr;
    splitter.max_rows = 1;
    splitter.max_cols = 1;
    splitter.row_count = 0;
    splitter.col_count = 0;
    splitter.has_h_scroll = false;
    splitter.has_v_scroll = false;
    splitter.tracking = false;
    splitter.tracking2 = false;
    splitter.hit_track = 0;
    splitter.track_offset = POINT{};
    splitter.rect_limit = RECT{};
    splitter.rect_tracker = RECT{};
    splitter.rect_tracker2 = RECT{};
    splitter.col_info.clear();
    splitter.row_info.clear();
    splitter.panes.clear();

    const bool win4_metrics = true;
    if (win4_metrics) {
        splitter.cx_splitter = 7;
        splitter.cy_splitter = 7;
        splitter.cx_border_share = 0;
        splitter.cy_border_share = 0;
        splitter.cx_splitter_gap = 7;
        splitter.cy_splitter_gap = 7;
        splitter.cx_border = 2;
        splitter.cy_border = 2;
    } else {
        splitter.cx_splitter = 4;
        splitter.cy_splitter = 4;
        splitter.cx_border_share = 1;
        splitter.cy_border_share = 1;
        splitter.cx_splitter_gap = 6;
        splitter.cy_splitter_gap = 6;
        splitter.cx_border = 0;
        splitter.cy_border = 0;
    }

    if (GetSystemMetrics(SM_CXBORDER) != 1 ||
        GetSystemMetrics(SM_CYBORDER) != 1) {
        AfxTraceOutput("Warning: CSplitterWnd assumes 1-pixel borders.\n");
    }
    return splitter;
}

void DestroySplitterWnd(MfcSplitterWndCompat& splitter) {
    splitter.col_info.clear();
    splitter.row_info.clear();
    splitter.panes.clear();
    splitter.dynamic_view_class = nullptr;
    DestroyCWndCompat(splitter);
}

MfcSplitterWndCompat* DeleteSplitterScalarDtor(MfcSplitterWndCompat* splitter,
    unsigned flags) {
    if (splitter == nullptr) {
        return nullptr;
    }
    DestroySplitterWnd(*splitter);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(splitter);
    }
    return splitter;
}

bool SplitterCreate(MfcSplitterWndCompat& splitter, MfcCWndCompat* parent,
    int max_rows, int max_cols, SIZE min_size,
    MfcCreateContextCompat* context, DWORD style, UINT id) {
    if (parent == nullptr || max_rows < 1 || max_cols < 1 ||
        max_rows > 2 || max_cols > 2 || (max_rows < 2 && max_cols < 2)) {
        return false;
    }
    splitter.max_rows = max_rows;
    splitter.max_cols = max_cols;
    splitter.row_count = 1;
    splitter.col_count = 1;
    splitter.dynamic_view_class = context == nullptr ? nullptr
        : context->new_view_class;
    if (!SplitterCreateCommon(splitter, parent, min_size.cx, min_size.cy,
        style, id)) {
        return false;
    }
    if (!SplitterCreateView(splitter, 0, 0, splitter.dynamic_view_class,
        min_size, context)) {
        CWndDestroyWindow(splitter);
        return false;
    }
    if (!splitter.col_info.empty()) {
        splitter.col_info[0].ideal_size = min_size.cx;
    }
    if (!splitter.row_info.empty()) {
        splitter.row_info[0].ideal_size = min_size.cy;
    }
    return true;
}

bool SplitterCreateStatic(MfcSplitterWndCompat& splitter, MfcCWndCompat* parent,
    int rows, int cols, DWORD style, UINT id) {
    if (parent == nullptr || rows < 1 || rows > 16 || cols < 1 ||
        cols > 16 || (rows < 2 && cols < 2)) {
        return false;
    }
    splitter.max_rows = rows;
    splitter.max_cols = cols;
    splitter.row_count = rows;
    splitter.col_count = cols;
    return SplitterCreateCommon(splitter, parent, 0, 0, style, id);
}

bool SplitterCreateCommon(MfcSplitterWndCompat& splitter, MfcCWndCompat* parent,
    int cx_min, int cy_min, DWORD style, UINT id) {
    if (parent == nullptr || parent->window == nullptr ||
        splitter.max_rows < 1 || splitter.max_cols < 1) {
        return false;
    }
    const DWORD create_style = style & ~static_cast<DWORD>(
        WS_HSCROLL | WS_VSCROLL);
    RECT rect{0, 0, 0, 0};
    if (!CreateAfxRegisteredWindow(splitter, "AfxMDIFrame42sd", nullptr,
        create_style, rect, parent->window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr)) {
        return false;
    }

    splitter.col_info.assign(static_cast<std::size_t>(splitter.max_cols),
        MfcSplitterPaneInfoCompat{cx_min, cx_min, -1});
    splitter.row_info.assign(static_cast<std::size_t>(splitter.max_rows),
        MfcSplitterPaneInfoCompat{cy_min, cy_min, -1});
    SplitterSetScrollStyle(splitter, style);
    return SplitterCreateCommonSuccessCleanup();
}

bool SplitterCreateCommonSuccessCleanup() {
    return true;
}

bool SplitterCreateView(MfcSplitterWndCompat& splitter, int row, int col,
    void* runtime_class, SIZE size, MfcCreateContextCompat* context) {
    if (row < 0 || row >= splitter.row_count || col < 0 ||
        col >= splitter.col_count || runtime_class == nullptr) {
        return false;
    }
    if (SplitterGetPane(splitter, row, col) != nullptr) {
        AfxTraceOutput("Error: CreateView pane already exists (%d,%d).\n",
            row, col);
        return false;
    }
    if (col < static_cast<int>(splitter.col_info.size())) {
        splitter.col_info[static_cast<std::size_t>(col)].ideal_size = size.cx;
    }
    if (row < static_cast<int>(splitter.row_info.size())) {
        splitter.row_info[static_cast<std::size_t>(row)].ideal_size = size.cy;
    }

    auto* runtime = static_cast<MfcRuntimeClassCompat*>(runtime_class);
    void* object = runtime->create_object == nullptr ? nullptr
        : RuntimeClassCreateObject(*runtime);
    auto* pane = static_cast<MfcCWndCompat*>(object);
    if (pane == nullptr) {
        auto* view = new MfcViewCompat();
        ConstructView(*view);
        pane = view;
    }
    return SplitterCreateViewFinish(splitter, *pane, row, col, size, context);
}

bool SplitterCreateViewFinish(MfcSplitterWndCompat& splitter,
    MfcCWndCompat& pane, int row, int col, SIZE size,
    MfcCreateContextCompat* context) {
    const UINT pane_id = static_cast<UINT>(
        SplitterIdFromRowCol(splitter, row, col));
    if (pane.window != nullptr) {
        return false;
    }

    MfcCreateContextCompat local_context{};
    if (context != nullptr) {
        local_context = *context;
    }
    local_context.current_frame = &splitter;
    local_context.new_view_class = local_context.new_view_class == nullptr
        ? splitter.dynamic_view_class : local_context.new_view_class;

    RECT rect{0, 0, std::max<LONG>(1, size.cx), std::max<LONG>(1, size.cy)};
    if (!CreateAfxRegisteredWindow(pane, "AfxFrameOrView42sd", nullptr,
        WS_CHILD | WS_VISIBLE, rect, splitter.window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(pane_id)),
        &local_context)) {
        AfxTraceOutput("Warning: couldn't create splitter pane (%d,%d).\n",
            row, col);
        return false;
    }

    if (ObjectIsKindOfRuntimeClass(&pane, GetViewRuntimeClass())) {
        auto& view = static_cast<MfcViewCompat&>(pane);
        view.active_frame = local_context.current_frame;
        view.document = local_context.current_doc;
    }
    splitter.panes.push_back(&pane);
    return true;
}

bool SplitterCreateScrollBarCtrl(MfcSplitterWndCompat& splitter,
    DWORD style, UINT id) {
    if (splitter.window == nullptr) {
        return false;
    }
    HWND scroll = CreateWindowExA(0, "SCROLLBAR", nullptr,
        style | WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, splitter.window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        GetModuleHandleA(nullptr), nullptr);
    if (scroll == nullptr) {
        AfxTraceOutput("Warning: Window creation failed: %lu\n",
            GetLastError());
        return false;
    }
    (void)CWndFromHandle(scroll);
    return true;
}

int SplitterIdFromRowCol(const MfcSplitterWndCompat& splitter, int row,
    int col) {
    if (row < 0 || row >= splitter.row_count || col < 0 ||
        col >= splitter.col_count) {
        return 0;
    }
    return 0xe900 + row * 16 + col;
}

MfcCWndCompat* SplitterGetPane(MfcSplitterWndCompat& splitter, int row,
    int col) {
    const int id = SplitterIdFromRowCol(splitter, row, col);
    if (id == 0 || splitter.window == nullptr) {
        return nullptr;
    }
    return CWndGetDlgItem(splitter, id);
}

bool SplitterIsChildPane(const MfcSplitterWndCompat& splitter,
    const MfcCWndCompat& pane, int* row, int* col) {
    if (row != nullptr) {
        *row = -1;
    }
    if (col != nullptr) {
        *col = -1;
    }
    if (splitter.window == nullptr || pane.window == nullptr ||
        IsChild(splitter.window, pane.window) == 0) {
        return false;
    }
    const UINT id = static_cast<UINT>(GetDlgCtrlID(pane.window)) & 0xffffU;
    if (id < 0xe900U || id > 0xe9ffU) {
        return false;
    }
    const int pane_row = static_cast<int>((id - 0xe900U) >> 4);
    const int pane_col = static_cast<int>((id - 0xe900U) & 0x0fU);
    if (pane_row >= splitter.row_count || pane_col >= splitter.col_count) {
        return false;
    }
    if (row != nullptr) {
        *row = pane_row;
    }
    if (col != nullptr) {
        *col = pane_col;
    }
    return true;
}

bool SplitterIsChildPaneInline(const MfcSplitterWndCompat& splitter,
    const MfcCWndCompat& pane, int* row, int* col) {
    return SplitterIsChildPane(splitter, pane, row, col);
}

void SplitterGetRowInfo(const MfcSplitterWndCompat& splitter, int row,
    int& current, int& minimum) {
    if (row < 0 || row >= splitter.max_rows ||
        row >= static_cast<int>(splitter.row_info.size())) {
        current = -1;
        minimum = 0;
        return;
    }
    const auto& info = splitter.row_info[static_cast<std::size_t>(row)];
    current = info.current_size;
    minimum = info.min_size;
}

void SplitterSetRowInfo(MfcSplitterWndCompat& splitter, int row,
    int ideal, int minimum) {
    if (row < 0 || row >= splitter.max_rows || ideal < 0 || minimum < 0) {
        return;
    }
    if (row >= static_cast<int>(splitter.row_info.size())) {
        splitter.row_info.resize(static_cast<std::size_t>(row + 1));
    }
    auto& info = splitter.row_info[static_cast<std::size_t>(row)];
    info.ideal_size = ideal;
    info.min_size = minimum;
}

void SplitterGetColumnInfo(const MfcSplitterWndCompat& splitter, int col,
    int& current, int& minimum) {
    if (col < 0 || col >= splitter.max_cols ||
        col >= static_cast<int>(splitter.col_info.size())) {
        current = -1;
        minimum = 0;
        return;
    }
    const auto& info = splitter.col_info[static_cast<std::size_t>(col)];
    current = info.current_size;
    minimum = info.min_size;
}

void SplitterSetColumnInfo(MfcSplitterWndCompat& splitter, int col,
    int ideal, int minimum) {
    if (col < 0 || col >= splitter.max_cols || ideal < 0 || minimum < 0) {
        return;
    }
    if (col >= static_cast<int>(splitter.col_info.size())) {
        splitter.col_info.resize(static_cast<std::size_t>(col + 1));
    }
    auto& info = splitter.col_info[static_cast<std::size_t>(col)];
    info.ideal_size = ideal;
    info.min_size = minimum;
}

DWORD SplitterGetScrollStyle(const MfcSplitterWndCompat& splitter) {
    DWORD style = 0;
    if (splitter.has_h_scroll) {
        style |= WS_HSCROLL;
    }
    if (splitter.has_v_scroll) {
        style |= WS_VSCROLL;
    }
    return style;
}

void SplitterSetScrollStyle(MfcSplitterWndCompat& splitter, DWORD style) {
    const DWORD requested = style & (WS_HSCROLL | WS_VSCROLL);
    if (SplitterGetScrollStyle(splitter) == requested) {
        return;
    }
    splitter.has_h_scroll = (requested & WS_HSCROLL) != 0;
    splitter.has_v_scroll = (requested & WS_VSCROLL) != 0;

    for (int col = 0; col < splitter.col_count; ++col) {
        const UINT id = static_cast<UINT>(0xea00 + col);
        MfcCWndCompat* scroll = CWndGetDlgItem(splitter, id);
        if (scroll == nullptr && splitter.has_h_scroll) {
            SplitterCreateScrollBarCtrl(splitter, SBS_HORZ, id);
            scroll = CWndGetDlgItem(splitter, id);
        }
        if (scroll != nullptr) {
            CWndShowWindow(*scroll, splitter.has_h_scroll ? SW_SHOW : SW_HIDE);
        }
    }
    for (int row = 0; row < splitter.row_count; ++row) {
        const UINT id = static_cast<UINT>(0xea10 + row);
        MfcCWndCompat* scroll = CWndGetDlgItem(splitter, id);
        if (scroll == nullptr && splitter.has_v_scroll) {
            SplitterCreateScrollBarCtrl(splitter, SBS_VERT, id);
            scroll = CWndGetDlgItem(splitter, id);
        }
        if (scroll != nullptr) {
            CWndShowWindow(*scroll, splitter.has_v_scroll ? SW_SHOW : SW_HIDE);
        }
    }

    MfcCWndCompat* size_box = CWndGetDlgItem(splitter, 0xea20);
    if (splitter.has_h_scroll && splitter.has_v_scroll) {
        if (size_box == nullptr) {
            SplitterCreateScrollBarCtrl(splitter, SBS_SIZEBOX, 0xea20);
            size_box = CWndGetDlgItem(splitter, 0xea20);
        }
        if (size_box != nullptr) {
            CWndShowWindow(*size_box, SW_SHOW);
        }
    } else if (size_box != nullptr) {
        CWndDestroyWindow(*size_box);
    }
}

void SplitterDeleteView(MfcSplitterWndCompat& splitter, int row, int col) {
    MfcCWndCompat* pane = SplitterGetPane(splitter, row, col);
    if (pane == nullptr) {
        return;
    }
    if (ObjectIsKindOfRuntimeClass(pane, GetViewRuntimeClass())) {
        auto& view = static_cast<MfcViewCompat&>(*pane);
        view.active_frame = nullptr;
    }
    CWndDestroyWindow(*pane);
    splitter.panes.erase(std::remove(splitter.panes.begin(),
        splitter.panes.end(), pane), splitter.panes.end());
}

namespace {

constexpr int kSplitterHitNone = 0;
constexpr int kSplitterHitVSplitBox = 1;
constexpr int kSplitterHitHSplitBox = 2;
constexpr int kSplitterHitBothSplitBox = 3;
constexpr int kSplitterHitRowBase = 0x65;
constexpr int kSplitterHitColumnBase = 0xc9;
constexpr int kSplitterHitIntersectionBase = 0x12d;

int rect_width(const RECT& rect) {
    return std::max<LONG>(0, rect.right - rect.left);
}

int rect_height(const RECT& rect) {
    return std::max<LONG>(0, rect.bottom - rect.top);
}

void clamp_rect(RECT& rect) {
    if (rect.right < rect.left) {
        rect.right = rect.left;
    }
    if (rect.bottom < rect.top) {
        rect.bottom = rect.top;
    }
}

int splitter_scroll_width() {
    return std::max(1, GetSystemMetrics(SM_CXVSCROLL));
}

int splitter_scroll_height() {
    return std::max(1, GetSystemMetrics(SM_CYHSCROLL));
}

int splitter_row_top(const MfcSplitterWndCompat& splitter, const RECT& inside,
    int row) {
    int y = inside.top;
    for (int index = 0; index < row &&
         index < static_cast<int>(splitter.row_info.size()); ++index) {
        y += std::max(0,
            splitter.row_info[static_cast<std::size_t>(index)].current_size);
        y += splitter.cy_splitter_gap;
    }
    return y;
}

int splitter_column_left(const MfcSplitterWndCompat& splitter,
    const RECT& inside, int col) {
    int x = inside.left;
    for (int index = 0; index < col &&
         index < static_cast<int>(splitter.col_info.size()); ++index) {
        x += std::max(0,
            splitter.col_info[static_cast<std::size_t>(index)].current_size);
        x += splitter.cx_splitter_gap;
    }
    return x;
}

bool splitter_valid_window(const MfcSplitterWndCompat& splitter) {
    return splitter.window != nullptr && IsWindow(splitter.window) != 0;
}

void splitter_set_child_id(MfcCWndCompat* child, int id) {
    if (child != nullptr && child->window != nullptr &&
        IsWindow(child->window)) {
        SetWindowLongA(child->window, GWL_ID, id);
    }
}

void splitter_compact_info(std::vector<MfcSplitterPaneInfoCompat>& values,
    int removed, int max_count) {
    if (removed < 0 || removed >= static_cast<int>(values.size())) {
        return;
    }
    for (int index = removed + 1; index < static_cast<int>(values.size());
         ++index) {
        values[static_cast<std::size_t>(index - 1)] =
            values[static_cast<std::size_t>(index)];
    }
    if (!values.empty()) {
        values[std::min<std::size_t>(values.size() - 1,
            static_cast<std::size_t>(std::max(0, max_count - 1)))] =
            MfcSplitterPaneInfoCompat{};
    }
    if (max_count > 0 && static_cast<int>(values.size()) < max_count) {
        values.resize(static_cast<std::size_t>(max_count));
    }
}

void splitter_invalidate_if_valid(const MfcSplitterWndCompat& splitter,
    const RECT* rect, BOOL erase) {
    if (splitter_valid_window(splitter)) {
        InvalidateRect(splitter.window, rect, erase);
    }
}

} // namespace

void SplitterOnDrawSplitter(MfcSplitterWndCompat& splitter, MfcCDCCompat* dc,
    int split_type, const RECT& rect) {
    RECT draw_rect = rect;
    clamp_rect(draw_rect);
    if (rect_width(draw_rect) == 0 || rect_height(draw_rect) == 0) {
        return;
    }
    if (dc == nullptr || dc->output_dc == nullptr) {
        splitter_invalidate_if_valid(splitter, &draw_rect, TRUE);
        return;
    }

    HBRUSH face = GetSysColorBrush(COLOR_BTNFACE);
    FillRect(dc->output_dc, &draw_rect, face);

    if (split_type == kSplitterHitRowBase ||
        split_type == kSplitterHitColumnBase ||
        split_type == kSplitterHitIntersectionBase ||
        rect_width(draw_rect) > splitter.cx_border ||
        rect_height(draw_rect) > splitter.cy_border) {
        RECT edge_rect = draw_rect;
        DrawEdge(dc->output_dc, &edge_rect, EDGE_RAISED, BF_RECT);
    }
}

int SplitterCanSplit(const MfcSplitterPaneInfoCompat& info,
    int before_size, int splitter_size) {
    const int current = std::max(info.current_size, info.ideal_size);
    const int minimum = std::max(0, info.min_size);
    const int after_size = current - before_size - std::max(0, splitter_size);
    if (before_size < minimum || after_size < minimum) {
        return -1;
    }
    return after_size;
}

bool SplitterSplitRow(MfcSplitterWndCompat& splitter, int before_size) {
    if (splitter.row_count <= 0 || splitter.row_count >= splitter.max_rows ||
        splitter.dynamic_view_class == nullptr || splitter.row_info.empty()) {
        return false;
    }

    auto& first = splitter.row_info.front();
    const int current = std::max(first.current_size, first.ideal_size);
    if (before_size <= 0) {
        before_size = current / 2;
    }
    int after_size = SplitterCanSplit(first, before_size,
        splitter.cy_splitter_gap);
    if (after_size < 0 && current > 0) {
        before_size = std::max(first.min_size, current / 2);
        after_size = SplitterCanSplit(first, before_size,
            splitter.cy_splitter_gap);
    }
    if (after_size < 0) {
        return false;
    }

    const int new_row = splitter.row_count;
    ++splitter.row_count;
    if (splitter.row_info.size() < static_cast<std::size_t>(splitter.max_rows)) {
        splitter.row_info.resize(static_cast<std::size_t>(splitter.max_rows));
    }
    first.ideal_size = before_size;
    first.current_size = before_size;
    splitter.row_info[static_cast<std::size_t>(new_row)] =
        MfcSplitterPaneInfoCompat{first.min_size, after_size, after_size};

    for (int col = 0; col < splitter.col_count; ++col) {
        const int width = col < static_cast<int>(splitter.col_info.size())
            ? std::max(1,
                splitter.col_info[static_cast<std::size_t>(col)].current_size)
            : 1;
        SIZE pane_size{width, std::max(1, after_size)};
        if (!SplitterCreateView(splitter, new_row, col,
            splitter.dynamic_view_class, pane_size, nullptr)) {
            for (int created_col = 0; created_col < col; ++created_col) {
                SplitterDeleteView(splitter, new_row, created_col);
            }
            --splitter.row_count;
            return false;
        }
    }
    SplitterRecalcLayout(splitter);
    return true;
}

bool SplitterSplitColumn(MfcSplitterWndCompat& splitter, int before_size) {
    if (splitter.col_count <= 0 || splitter.col_count >= splitter.max_cols ||
        splitter.dynamic_view_class == nullptr || splitter.col_info.empty()) {
        return false;
    }

    auto& first = splitter.col_info.front();
    const int current = std::max(first.current_size, first.ideal_size);
    if (before_size <= 0) {
        before_size = current / 2;
    }
    int after_size = SplitterCanSplit(first, before_size,
        splitter.cx_splitter_gap);
    if (after_size < 0 && current > 0) {
        before_size = std::max(first.min_size, current / 2);
        after_size = SplitterCanSplit(first, before_size,
            splitter.cx_splitter_gap);
    }
    if (after_size < 0) {
        return false;
    }

    const int new_col = splitter.col_count;
    ++splitter.col_count;
    if (splitter.col_info.size() < static_cast<std::size_t>(splitter.max_cols)) {
        splitter.col_info.resize(static_cast<std::size_t>(splitter.max_cols));
    }
    first.ideal_size = before_size;
    first.current_size = before_size;
    splitter.col_info[static_cast<std::size_t>(new_col)] =
        MfcSplitterPaneInfoCompat{first.min_size, after_size, after_size};

    for (int row = 0; row < splitter.row_count; ++row) {
        const int height = row < static_cast<int>(splitter.row_info.size())
            ? std::max(1,
                splitter.row_info[static_cast<std::size_t>(row)].current_size)
            : 1;
        SIZE pane_size{std::max(1, after_size), height};
        if (!SplitterCreateView(splitter, row, new_col,
            splitter.dynamic_view_class, pane_size, nullptr)) {
            for (int created_row = 0; created_row < row; ++created_row) {
                SplitterDeleteView(splitter, created_row, new_col);
            }
            --splitter.col_count;
            return false;
        }
    }
    SplitterRecalcLayout(splitter);
    return true;
}

void SplitterDeleteRow(MfcSplitterWndCompat& splitter, int row) {
    if (row < 0 || row >= splitter.row_count || splitter.row_count <= 1) {
        return;
    }
    for (int col = 0; col < splitter.col_count; ++col) {
        SplitterDeleteView(splitter, row, col);
    }
    for (int move_row = row + 1; move_row < splitter.row_count; ++move_row) {
        for (int col = 0; col < splitter.col_count; ++col) {
            splitter_set_child_id(SplitterGetPane(splitter, move_row, col),
                SplitterIdFromRowCol(splitter, move_row - 1, col));
        }
        splitter_set_child_id(CWndGetDlgItem(splitter, 0xea10 + move_row),
            0xea10 + move_row - 1);
    }
    if (row > 0 && row < static_cast<int>(splitter.row_info.size())) {
        auto& previous = splitter.row_info[static_cast<std::size_t>(row - 1)];
        previous.ideal_size += splitter.cy_splitter_gap +
            std::max(0,
                splitter.row_info[static_cast<std::size_t>(row)].ideal_size);
    }
    --splitter.row_count;
    splitter_compact_info(splitter.row_info, row, splitter.max_rows);
    SplitterRecalcLayout(splitter);
}

void SplitterDeleteColumn(MfcSplitterWndCompat& splitter, int col) {
    if (col < 0 || col >= splitter.col_count || splitter.col_count <= 1) {
        return;
    }
    for (int row = 0; row < splitter.row_count; ++row) {
        SplitterDeleteView(splitter, row, col);
    }
    for (int move_col = col + 1; move_col < splitter.col_count; ++move_col) {
        for (int row = 0; row < splitter.row_count; ++row) {
            splitter_set_child_id(SplitterGetPane(splitter, row, move_col),
                SplitterIdFromRowCol(splitter, row, move_col - 1));
        }
        splitter_set_child_id(CWndGetDlgItem(splitter, 0xea00 + move_col),
            0xea00 + move_col - 1);
    }
    if (col > 0 && col < static_cast<int>(splitter.col_info.size())) {
        auto& previous = splitter.col_info[static_cast<std::size_t>(col - 1)];
        previous.ideal_size += splitter.cx_splitter_gap +
            std::max(0,
                splitter.col_info[static_cast<std::size_t>(col)].ideal_size);
    }
    --splitter.col_count;
    splitter_compact_info(splitter.col_info, col, splitter.max_cols);
    SplitterRecalcLayout(splitter);
}

void SplitterGetInsideRect(const MfcSplitterWndCompat& splitter, RECT& rect) {
    SetRectEmpty(&rect);
    if (!splitter_valid_window(splitter)) {
        return;
    }
    GetClientRect(splitter.window, &rect);
    rect.left += splitter.cx_border;
    rect.top += splitter.cy_border;
    rect.right -= splitter.cx_border;
    rect.bottom -= splitter.cy_border;
    if (splitter.has_v_scroll) {
        rect.right -= splitter_scroll_width();
    }
    if (splitter.has_h_scroll) {
        rect.bottom -= splitter_scroll_height();
    }
    clamp_rect(rect);
}

void SplitterTrackRowSize(MfcSplitterWndCompat& splitter, int y, int row) {
    if (row < 0 || row >= splitter.row_count ||
        row >= static_cast<int>(splitter.row_info.size())) {
        return;
    }
    RECT inside{};
    SplitterGetInsideRect(splitter, inside);
    const int top = splitter_row_top(splitter, inside, row);
    const int new_size = y - top - splitter.track_offset.y;
    auto& info = splitter.row_info[static_cast<std::size_t>(row)];
    if (new_size < info.min_size / 2 && splitter.row_count > 1) {
        SplitterDeleteRow(splitter,
            row + 1 < splitter.row_count ? row + 1 : row);
        return;
    }
    const int previous = std::max(0, info.ideal_size);
    info.ideal_size = std::max(info.min_size, new_size);
    if (row + 1 < splitter.row_count &&
        row + 1 < static_cast<int>(splitter.row_info.size())) {
        auto& next = splitter.row_info[static_cast<std::size_t>(row + 1)];
        next.ideal_size = std::max(next.min_size,
            next.ideal_size - (info.ideal_size - previous));
    }
}

void SplitterTrackColumnSize(MfcSplitterWndCompat& splitter, int x, int col) {
    if (col < 0 || col >= splitter.col_count ||
        col >= static_cast<int>(splitter.col_info.size())) {
        return;
    }
    RECT inside{};
    SplitterGetInsideRect(splitter, inside);
    const int left = splitter_column_left(splitter, inside, col);
    const int new_size = x - left - splitter.track_offset.x;
    auto& info = splitter.col_info[static_cast<std::size_t>(col)];
    if (new_size < info.min_size / 2 && splitter.col_count > 1) {
        SplitterDeleteColumn(splitter,
            col + 1 < splitter.col_count ? col + 1 : col);
        return;
    }
    const int previous = std::max(0, info.ideal_size);
    info.ideal_size = std::max(info.min_size, new_size);
    if (col + 1 < splitter.col_count &&
        col + 1 < static_cast<int>(splitter.col_info.size())) {
        auto& next = splitter.col_info[static_cast<std::size_t>(col + 1)];
        next.ideal_size = std::max(next.min_size,
            next.ideal_size - (info.ideal_size - previous));
    }
}

void SplitterGetHitRect(MfcSplitterWndCompat& splitter, int hit_test,
    RECT& rect) {
    SetRectEmpty(&rect);
    RECT inside{};
    SplitterGetInsideRect(splitter, inside);
    if (rect_width(inside) == 0 || rect_height(inside) == 0) {
        return;
    }

    if (hit_test == kSplitterHitVSplitBox ||
        hit_test == kSplitterHitBothSplitBox) {
        rect = RECT{inside.right - splitter.cx_splitter, inside.top,
            inside.right, inside.top + splitter.cy_splitter};
        clamp_rect(rect);
        return;
    }
    if (hit_test == kSplitterHitHSplitBox) {
        rect = RECT{inside.left, inside.bottom - splitter.cy_splitter,
            inside.left + splitter.cx_splitter, inside.bottom};
        clamp_rect(rect);
        return;
    }

    if (hit_test >= kSplitterHitIntersectionBase) {
        const int encoded = hit_test - kSplitterHitIntersectionBase;
        const int row = encoded / 0x0f;
        const int col = encoded % 0x0f;
        if (row >= 0 && row < splitter.row_count - 1 &&
            col >= 0 && col < splitter.col_count - 1) {
            const int x = splitter_column_left(splitter, inside, col) +
                splitter.col_info[static_cast<std::size_t>(col)].current_size;
            const int y = splitter_row_top(splitter, inside, row) +
                splitter.row_info[static_cast<std::size_t>(row)].current_size;
            rect = RECT{x, y, x + splitter.cx_splitter_gap,
                y + splitter.cy_splitter_gap};
            clamp_rect(rect);
        }
        return;
    }

    if (hit_test >= kSplitterHitColumnBase) {
        const int col = hit_test - kSplitterHitColumnBase;
        if (col >= 0 && col < splitter.col_count - 1 &&
            col < static_cast<int>(splitter.col_info.size())) {
            const int x = splitter_column_left(splitter, inside, col) +
                splitter.col_info[static_cast<std::size_t>(col)].current_size;
            rect = RECT{x, inside.top, x + splitter.cx_splitter_gap,
                inside.bottom};
            clamp_rect(rect);
        }
        return;
    }

    if (hit_test >= kSplitterHitRowBase) {
        const int row = hit_test - kSplitterHitRowBase;
        if (row >= 0 && row < splitter.row_count - 1 &&
            row < static_cast<int>(splitter.row_info.size())) {
            const int y = splitter_row_top(splitter, inside, row) +
                splitter.row_info[static_cast<std::size_t>(row)].current_size;
            rect = RECT{inside.left, y, inside.right,
                y + splitter.cy_splitter_gap};
            clamp_rect(rect);
        }
    }
}

int SplitterHitTest(MfcSplitterWndCompat& splitter, POINT point) {
    RECT rect{};
    for (int row = 0; row < splitter.row_count - 1; ++row) {
        for (int col = 0; col < splitter.col_count - 1; ++col) {
            const int hit = kSplitterHitIntersectionBase + row * 0x0f + col;
            SplitterGetHitRect(splitter, hit, rect);
            if (PtInRect(&rect, point)) {
                return hit;
            }
        }
    }
    for (int col = 0; col < splitter.col_count - 1; ++col) {
        const int hit = kSplitterHitColumnBase + col;
        SplitterGetHitRect(splitter, hit, rect);
        if (PtInRect(&rect, point)) {
            return hit;
        }
    }
    for (int row = 0; row < splitter.row_count - 1; ++row) {
        const int hit = kSplitterHitRowBase + row;
        SplitterGetHitRect(splitter, hit, rect);
        if (PtInRect(&rect, point)) {
            return hit;
        }
    }
    if (splitter.max_cols > splitter.col_count) {
        SplitterGetHitRect(splitter, kSplitterHitVSplitBox, rect);
        if (PtInRect(&rect, point)) {
            return kSplitterHitVSplitBox;
        }
    }
    if (splitter.max_rows > splitter.row_count) {
        SplitterGetHitRect(splitter, kSplitterHitHSplitBox, rect);
        if (PtInRect(&rect, point)) {
            return kSplitterHitHSplitBox;
        }
    }
    return kSplitterHitNone;
}

void SplitterInvertTracker(MfcSplitterWndCompat& splitter, const RECT& rect) {
    if (!splitter_valid_window(splitter)) {
        return;
    }
    RECT draw_rect = rect;
    clamp_rect(draw_rect);
    if (rect_width(draw_rect) == 0 || rect_height(draw_rect) == 0) {
        return;
    }
    HDC dc = GetDC(splitter.window);
    if (dc == nullptr) {
        return;
    }
    PatBlt(dc, draw_rect.left, draw_rect.top, rect_width(draw_rect),
        rect_height(draw_rect), DSTINVERT);
    ReleaseDC(splitter.window, dc);
}

bool SplitterDoKeyboardSplit(MfcSplitterWndCompat& splitter) {
    if (!splitter_valid_window(splitter)) {
        return false;
    }
    RECT inside{};
    SplitterGetInsideRect(splitter, inside);
    POINT point{inside.left + rect_width(inside) / 2,
        inside.top + rect_height(inside) / 2};
    if (splitter.max_cols > splitter.col_count &&
        SplitterSplitColumn(splitter, point.x - inside.left)) {
        return true;
    }
    if (splitter.max_rows > splitter.row_count &&
        SplitterSplitRow(splitter, point.y - inside.top)) {
        return true;
    }
    return false;
}

void SplitterLayoutRowCol(std::vector<MfcSplitterPaneInfoCompat>& info,
    int count, int total_size, int splitter_gap) {
    if (count <= 0) {
        return;
    }
    if (static_cast<int>(info.size()) < count) {
        info.resize(static_cast<std::size_t>(count));
    }
    const int available = std::max(0,
        total_size - std::max(0, count - 1) * std::max(0, splitter_gap));
    int min_sum = 0;
    int ideal_sum = 0;
    for (int index = 0; index < count; ++index) {
        auto& value = info[static_cast<std::size_t>(index)];
        value.min_size = std::max(0, value.min_size);
        value.ideal_size = std::max(value.min_size, value.ideal_size);
        min_sum += value.min_size;
        ideal_sum += std::max(1, value.ideal_size);
    }

    if (available <= min_sum) {
        int remaining = available;
        for (int index = 0; index < count; ++index) {
            auto& value = info[static_cast<std::size_t>(index)];
            value.current_size = std::max(0,
                std::min(value.min_size, remaining));
            remaining -= value.current_size;
        }
        return;
    }

    int remaining = available;
    int remaining_weight = ideal_sum;
    for (int index = 0; index < count; ++index) {
        auto& value = info[static_cast<std::size_t>(index)];
        int current = index == count - 1 ? remaining :
            (remaining * std::max(1, value.ideal_size)) /
                std::max(1, remaining_weight);
        current = std::max(value.min_size, current);
        current = std::min(current, remaining);
        value.current_size = current;
        remaining -= current;
        remaining_weight -= std::max(1, value.ideal_size);
    }
    for (int index = count - 1; remaining > 0 && index >= 0; --index) {
        auto& value = info[static_cast<std::size_t>(index)];
        ++value.current_size;
        --remaining;
    }
}

void SplitterDeferClientPos(HDWP* hdwp, MfcCWndCompat& window,
    int x, int y, int cx, int cy, bool scroll_bar) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return;
    }
    cx = std::max(0, cx);
    cy = std::max(0, cy);
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (scroll_bar) {
        flags |= (cx == 0 || cy == 0) ? SWP_HIDEWINDOW : SWP_SHOWWINDOW;
    }
    if (hdwp != nullptr && *hdwp != nullptr) {
        *hdwp = DeferWindowPos(*hdwp, window.window, nullptr, x, y, cx, cy,
            flags);
        return;
    }
    SetWindowPos(window.window, nullptr, x, y, cx, cy, flags);
}

MfcCWndCompat* SplitterGetSizingParent(MfcSplitterWndCompat& splitter) {
    if (!splitter_valid_window(splitter)) {
        return nullptr;
    }
    HWND parent = GetParent(splitter.window);
    if (parent == nullptr) {
        return nullptr;
    }
    HWND root = GetAncestor(parent, GA_ROOT);
    HWND target = root != nullptr ? root : parent;
    const DWORD style = static_cast<DWORD>(GetWindowLongA(target, GWL_STYLE));
    if ((style & WS_THICKFRAME) == 0) {
        target = parent;
    }
    return CWndFromHandle(target);
}

void SplitterRecalcLayout(MfcSplitterWndCompat& splitter) {
    if (!splitter_valid_window(splitter) || splitter.row_count <= 0 ||
        splitter.col_count <= 0) {
        return;
    }
    RECT client{};
    GetClientRect(splitter.window, &client);
    RECT inside{};
    SplitterGetInsideRect(splitter, inside);

    SplitterLayoutRowCol(splitter.col_info, splitter.col_count,
        rect_width(inside), splitter.cx_splitter_gap);
    SplitterLayoutRowCol(splitter.row_info, splitter.row_count,
        rect_height(inside), splitter.cy_splitter_gap);

    const int defer_count = splitter.row_count * splitter.col_count +
        splitter.row_count + splitter.col_count + 1;
    HDWP hdwp = BeginDeferWindowPos(std::max(1, defer_count));

    int y = inside.top;
    for (int row = 0; row < splitter.row_count; ++row) {
        int x = inside.left;
        const int height = row < static_cast<int>(splitter.row_info.size())
            ? splitter.row_info[static_cast<std::size_t>(row)].current_size
            : 0;
        for (int col = 0; col < splitter.col_count; ++col) {
            const int width = col < static_cast<int>(splitter.col_info.size())
                ? splitter.col_info[static_cast<std::size_t>(col)].current_size
                : 0;
            MfcCWndCompat* pane = SplitterGetPane(splitter, row, col);
            if (pane != nullptr) {
                SplitterDeferClientPos(&hdwp, *pane, x, y, width, height,
                    false);
            }
            x += width + splitter.cx_splitter_gap;
        }
        y += height + splitter.cy_splitter_gap;
    }

    if (splitter.has_h_scroll) {
        int x = inside.left;
        const int scroll_y = std::max<int>(0,
            static_cast<int>(client.bottom) - splitter_scroll_height());
        for (int col = 0; col < splitter.col_count; ++col) {
            const int width = col < static_cast<int>(splitter.col_info.size())
                ? splitter.col_info[static_cast<std::size_t>(col)].current_size
                : 0;
            MfcCWndCompat* scroll = CWndGetDlgItem(splitter, 0xea00 + col);
            if (scroll != nullptr) {
                SplitterDeferClientPos(&hdwp, *scroll, x, scroll_y, width,
                    splitter_scroll_height(), true);
            }
            x += width + splitter.cx_splitter_gap;
        }
    }
    if (splitter.has_v_scroll) {
        int y_scroll = inside.top;
        const int scroll_x = std::max<int>(0,
            static_cast<int>(client.right) - splitter_scroll_width());
        for (int row = 0; row < splitter.row_count; ++row) {
            const int height = row < static_cast<int>(splitter.row_info.size())
                ? splitter.row_info[static_cast<std::size_t>(row)].current_size
                : 0;
            MfcCWndCompat* scroll = CWndGetDlgItem(splitter, 0xea10 + row);
            if (scroll != nullptr) {
                SplitterDeferClientPos(&hdwp, *scroll, scroll_x, y_scroll,
                    splitter_scroll_width(), height, true);
            }
            y_scroll += height + splitter.cy_splitter_gap;
        }
    }
    MfcCWndCompat* size_box = CWndGetDlgItem(splitter, 0xea20);
    if (size_box != nullptr) {
        SplitterDeferClientPos(&hdwp, *size_box,
            std::max<int>(0,
                static_cast<int>(client.right) - splitter_scroll_width()),
            std::max<int>(0,
                static_cast<int>(client.bottom) - splitter_scroll_height()),
            splitter.has_v_scroll ? splitter_scroll_width() : 0,
            splitter.has_h_scroll ? splitter_scroll_height() : 0, true);
    }

    if (hdwp != nullptr && EndDeferWindowPos(hdwp) == 0) {
        AfxTraceOutput("Warning: splitter DeferWindowPos failed.\n");
    }
    InvalidateRect(splitter.window, nullptr, TRUE);
}

void SplitterDrawAllSplitBars(MfcSplitterWndCompat& splitter,
    MfcCDCCompat& dc, int inside_left, int inside_top) {
    (void)inside_left;
    (void)inside_top;
    RECT rect{};
    for (int col = 0; col < splitter.col_count - 1; ++col) {
        SplitterGetHitRect(splitter, kSplitterHitColumnBase + col, rect);
        SplitterOnDrawSplitter(splitter, &dc, kSplitterHitColumnBase, rect);
    }
    for (int row = 0; row < splitter.row_count - 1; ++row) {
        SplitterGetHitRect(splitter, kSplitterHitRowBase + row, rect);
        SplitterOnDrawSplitter(splitter, &dc, kSplitterHitRowBase, rect);
    }
    for (int row = 0; row < splitter.row_count - 1; ++row) {
        for (int col = 0; col < splitter.col_count - 1; ++col) {
            SplitterGetHitRect(splitter,
                kSplitterHitIntersectionBase + row * 0x0f + col, rect);
            SplitterOnDrawSplitter(splitter, &dc,
                kSplitterHitIntersectionBase, rect);
        }
    }
}

void SplitterOnPaint(MfcSplitterWndCompat& splitter) {
    if (!splitter_valid_window(splitter)) {
        return;
    }
    MfcWindowDCCompat paint_dc{};
    ConstructPaintDC(paint_dc, splitter.window);
    RECT inside{};
    SplitterGetInsideRect(splitter, inside);
    SplitterDrawAllSplitBars(splitter, paint_dc, inside.left, inside.top);
    DestroyPaintDC(paint_dc);
}

void SplitterSetSplitCursor(int hit_test) {
    LPCSTR cursor_name = IDC_ARROW;
    if (hit_test >= kSplitterHitIntersectionBase ||
        hit_test == kSplitterHitBothSplitBox) {
        cursor_name = IDC_SIZEALL;
    } else if (hit_test >= kSplitterHitColumnBase ||
        hit_test == kSplitterHitVSplitBox) {
        cursor_name = IDC_SIZEWE;
    } else if (hit_test >= kSplitterHitRowBase ||
        hit_test == kSplitterHitHSplitBox) {
        cursor_name = IDC_SIZENS;
    }
    SetCursor(LoadCursorA(nullptr, cursor_name));
}

void SplitterOnLButtonDblClk(MfcSplitterWndCompat& splitter, UINT flags,
    POINT point) {
    (void)flags;
    const int hit = SplitterHitTest(splitter, point);
    if (hit == kSplitterHitVSplitBox) {
        RECT inside{};
        SplitterGetInsideRect(splitter, inside);
        SplitterSplitColumn(splitter, rect_width(inside) / 2);
    } else if (hit == kSplitterHitHSplitBox) {
        RECT inside{};
        SplitterGetInsideRect(splitter, inside);
        SplitterSplitRow(splitter, rect_height(inside) / 2);
    } else if (hit >= kSplitterHitColumnBase &&
        hit < kSplitterHitIntersectionBase) {
        const int col = hit - kSplitterHitColumnBase;
        SplitterDeleteColumn(splitter,
            col + 1 < splitter.col_count ? col + 1 : col);
    } else if (hit >= kSplitterHitRowBase &&
        hit < kSplitterHitColumnBase) {
        const int row = hit - kSplitterHitRowBase;
        SplitterDeleteRow(splitter,
            row + 1 < splitter.row_count ? row + 1 : row);
    }
}

void StartTracking(MfcSplitterWndCompat& splitter, int hit_test) {
    if (!splitter_valid_window(splitter)) {
        return;
    }
    RECT tracker{};
    SplitterGetHitRect(splitter, hit_test, tracker);
    if (RectIsEmpty(tracker)) {
        return;
    }
    SplitterGetInsideRect(splitter, splitter.rect_limit);
    splitter.hit_track = hit_test;
    splitter.rect_tracker = tracker;
    splitter.rect_tracker2 = RECT{};
    splitter.tracking = true;
    splitter.tracking2 = false;
    splitter.track_offset = POINT{};
    SetCapture(splitter.window);
    SplitterInvertTracker(splitter, splitter.rect_tracker);
}

void OnMouseMove(MfcSplitterWndCompat& splitter, UINT flags, POINT point) {
    (void)flags;
    if (!splitter.tracking) {
        SplitterSetSplitCursor(SplitterHitTest(splitter, point));
        return;
    }

    RECT next = splitter.rect_tracker;
    const int width = static_cast<int>(RectWidth(next));
    const int height = static_cast<int>(RectHeight(next));
    int center_x = point.x - splitter.track_offset.x;
    int center_y = point.y - splitter.track_offset.y;
    if (!RectIsEmpty(splitter.rect_limit)) {
        center_x = std::clamp(center_x,
            static_cast<int>(splitter.rect_limit.left),
            static_cast<int>(splitter.rect_limit.right));
        center_y = std::clamp(center_y,
            static_cast<int>(splitter.rect_limit.top),
            static_cast<int>(splitter.rect_limit.bottom));
    }

    if (splitter.hit_track >= kSplitterHitColumnBase &&
        splitter.hit_track < kSplitterHitIntersectionBase) {
        OffsetRect(&next, center_x - (next.left + next.right) / 2, 0);
    } else if (splitter.hit_track >= kSplitterHitRowBase &&
        splitter.hit_track < kSplitterHitColumnBase) {
        OffsetRect(&next, 0, center_y - (next.top + next.bottom) / 2);
    } else {
        next.left = center_x - width / 2;
        next.right = next.left + width;
        next.top = center_y - height / 2;
        next.bottom = next.top + height;
    }

    if (EqualRect(&next, &splitter.rect_tracker)) {
        return;
    }
    SplitterInvertTracker(splitter, splitter.rect_tracker);
    splitter.rect_tracker = next;
    SplitterInvertTracker(splitter, splitter.rect_tracker);
}

void SplitterStopTrackingAccept(MfcSplitterWndCompat& splitter) {
    if (!splitter.tracking) {
        return;
    }
    SplitterInvertTracker(splitter, splitter.rect_tracker);
    if (splitter.tracking2) {
        SplitterInvertTracker(splitter, splitter.rect_tracker2);
    }
    const int hit = splitter.hit_track;
    const POINT center{
        (splitter.rect_tracker.left + splitter.rect_tracker.right) / 2,
        (splitter.rect_tracker.top + splitter.rect_tracker.bottom) / 2};
    if (hit >= kSplitterHitIntersectionBase) {
        const int encoded = hit - kSplitterHitIntersectionBase;
        SplitterTrackRowSize(splitter, center.y, encoded / 0x0f);
        SplitterTrackColumnSize(splitter, center.x, encoded % 0x0f);
    } else if (hit >= kSplitterHitColumnBase) {
        SplitterTrackColumnSize(splitter, center.x,
            hit - kSplitterHitColumnBase);
    } else if (hit >= kSplitterHitRowBase) {
        SplitterTrackRowSize(splitter, center.y, hit - kSplitterHitRowBase);
    }
    splitter.tracking = false;
    splitter.tracking2 = false;
    splitter.hit_track = 0;
    if (GetCapture() == splitter.window) {
        ReleaseCapture();
    }
    SplitterRecalcLayout(splitter);
}

void SplitterStopTrackingCancel(MfcSplitterWndCompat& splitter) {
    if (!splitter.tracking) {
        return;
    }
    SplitterInvertTracker(splitter, splitter.rect_tracker);
    if (splitter.tracking2) {
        SplitterInvertTracker(splitter, splitter.rect_tracker2);
    }
    splitter.tracking = false;
    splitter.tracking2 = false;
    splitter.hit_track = 0;
    if (GetCapture() == splitter.window) {
        ReleaseCapture();
    }
}

void SplitterOnKeyDown(MfcSplitterWndCompat& splitter, UINT key) {
    if (!splitter.tracking) {
        if (key == VK_RETURN || key == VK_SPACE) {
            SplitterDoKeyboardSplit(splitter);
        }
        return;
    }

    if (key == VK_ESCAPE) {
        SplitterStopTrackingCancel(splitter);
        return;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        SplitterStopTrackingAccept(splitter);
        return;
    }

    int dx = 0;
    int dy = 0;
    if (key == VK_LEFT) {
        dx = -1;
    } else if (key == VK_RIGHT) {
        dx = 1;
    } else if (key == VK_UP) {
        dy = -1;
    } else if (key == VK_DOWN) {
        dy = 1;
    }
    if (dx == 0 && dy == 0) {
        return;
    }
    SplitterInvertTracker(splitter, splitter.rect_tracker);
    OffsetRect(&splitter.rect_tracker, dx, dy);
    SplitterInvertTracker(splitter, splitter.rect_tracker);
}

namespace {

MfcCWndCompat* splitter_parent_frame(MfcSplitterWndCompat& splitter) {
    if (!splitter_valid_window(splitter)) {
        return nullptr;
    }
    HWND current = GetParent(splitter.window);
    HWND last = nullptr;
    while (current != nullptr) {
        last = current;
        const DWORD style =
            static_cast<DWORD>(GetWindowLongA(current, GWL_STYLE));
        if ((style & WS_CHILD) == 0) {
            return CWndFromHandle(current);
        }
        current = GetParent(current);
    }
    return last == nullptr ? nullptr : CWndFromHandle(last);
}

MfcScrollViewCompat* splitter_scroll_view(MfcCWndCompat* pane) {
    if (pane != nullptr &&
        ObjectIsKindOfRuntimeClass(pane, GetScrollViewRuntimeClass())) {
        return static_cast<MfcScrollViewCompat*>(pane);
    }
    return nullptr;
}

void splitter_send_axis_scroll(MfcCWndCompat* pane, bool vertical, UINT code,
    UINT position, MfcCWndCompat* scroll_bar) {
    if (pane == nullptr || pane->window == nullptr ||
        !IsWindow(pane->window)) {
        return;
    }
    if (MfcScrollViewCompat* view = splitter_scroll_view(pane)) {
        if (vertical) {
            ScrollViewOnVScroll(*view, code, position, scroll_bar);
        } else {
            ScrollViewOnHScroll(*view, code, position, scroll_bar);
        }
        return;
    }
    const UINT message = vertical ? WM_VSCROLL : WM_HSCROLL;
    SendMessageA(pane->window, message, MAKEWPARAM(code, position),
        reinterpret_cast<LPARAM>(
            scroll_bar == nullptr ? nullptr : scroll_bar->window));
}

bool splitter_scroll_pane(MfcCWndCompat* pane, UINT scroll_code,
    UINT position, bool do_scroll) {
    if (MfcScrollViewCompat* view = splitter_scroll_view(pane)) {
        return ScrollViewOnScroll(*view, scroll_code, position, do_scroll);
    }
    if (pane != nullptr && pane->window != nullptr && IsWindow(pane->window)) {
        const UINT h_code = scroll_code & 0xffU;
        const UINT v_code = (scroll_code >> 8) & 0xffU;
        if (h_code != 0xffU) {
            SendMessageA(pane->window, WM_HSCROLL, MAKEWPARAM(h_code, position),
                0);
        }
        if (v_code != 0xffU) {
            SendMessageA(pane->window, WM_VSCROLL, MAKEWPARAM(v_code, position),
                0);
        }
        return true;
    }
    return false;
}

bool splitter_scroll_pane_by(MfcCWndCompat* pane, SIZE scroll_size,
    bool do_scroll) {
    if (MfcScrollViewCompat* view = splitter_scroll_view(pane)) {
        return ScrollViewOnScrollBy(*view, scroll_size, do_scroll);
    }
    if (pane != nullptr && pane->window != nullptr && IsWindow(pane->window)) {
        if (do_scroll) {
            CWndScrollWindowCompat(*pane, -scroll_size.cx, -scroll_size.cy,
                nullptr, nullptr);
        }
        return scroll_size.cx != 0 || scroll_size.cy != 0;
    }
    return false;
}

} // namespace

bool SplitterOnCommand(MfcSplitterWndCompat& splitter, WPARAM wparam,
    HWND control) {
    if (CWndOnCommand(splitter, wparam, control)) {
        return true;
    }
    MfcCWndCompat* parent = splitter_parent_frame(splitter);
    if (parent == nullptr || parent->window == nullptr) {
        return false;
    }
    return SendMessageA(parent->window, WM_COMMAND, wparam,
        reinterpret_cast<LPARAM>(control)) != 0;
}

bool SplitterOnNotify(MfcSplitterWndCompat& splitter, WPARAM control_id,
    NMHDR* notify, LRESULT* result) {
    if (CWndOnNotify(splitter, control_id, notify, result)) {
        return true;
    }
    MfcCWndCompat* parent = splitter_parent_frame(splitter);
    LRESULT forwarded = 0;
    if (parent != nullptr && parent->window != nullptr) {
        forwarded = SendMessageA(parent->window, WM_NOTIFY, control_id,
            reinterpret_cast<LPARAM>(notify));
    }
    if (result != nullptr) {
        *result = forwarded;
    }
    return true;
}

bool SplitterOnMouseWheel(MfcSplitterWndCompat& splitter, UINT flags,
    short delta, POINT point) {
    bool handled = false;
    for (int row = 0; row < splitter.row_count; ++row) {
        for (int col = 0; col < splitter.col_count; ++col) {
            MfcScrollViewCompat* view =
                splitter_scroll_view(SplitterGetPane(splitter, row, col));
            if (view != nullptr &&
                ScrollViewDoMouseWheel(*view, flags, delta, point)) {
                handled = true;
            }
        }
    }
    return handled || splitter.row_count * splitter.col_count > 0;
}

int SplitterGetRowCount(const MfcSplitterWndCompat& splitter) {
    return splitter.row_count;
}

int SplitterGetColumnCount(const MfcSplitterWndCompat& splitter) {
    return splitter.col_count;
}

void SplitterOnHScroll(MfcSplitterWndCompat& splitter, UINT code,
    UINT position, MfcCWndCompat* scroll_bar) {
    if (scroll_bar == nullptr || scroll_bar->window == nullptr) {
        return;
    }
    const int col =
        (static_cast<int>(GetDlgCtrlID(scroll_bar->window)) & 0xffff) - 0xea00;
    if (col < 0 || col >= splitter.col_count) {
        return;
    }
    for (int row = 0; row < splitter.row_count; ++row) {
        splitter_send_axis_scroll(SplitterGetPane(splitter, row, col), false,
            code, position, scroll_bar);
    }
}

void SplitterOnVScroll(MfcSplitterWndCompat& splitter, UINT code,
    UINT position, MfcCWndCompat* scroll_bar) {
    if (scroll_bar == nullptr || scroll_bar->window == nullptr) {
        return;
    }
    const int row =
        (static_cast<int>(GetDlgCtrlID(scroll_bar->window)) & 0xffff) - 0xea10;
    if (row < 0 || row >= splitter.row_count) {
        return;
    }
    for (int col = 0; col < splitter.col_count; ++col) {
        splitter_send_axis_scroll(SplitterGetPane(splitter, row, col), true,
            code, position, scroll_bar);
    }
}

bool SplitterOnScroll(MfcSplitterWndCompat& splitter, MfcCWndCompat& pane,
    UINT scroll_code, UINT position, bool do_scroll) {
    int row = -1;
    int col = -1;
    if (!SplitterIsChildPane(splitter, pane, &row, &col)) {
        return false;
    }

    bool handled = splitter_scroll_pane(&pane, scroll_code, position,
        do_scroll);
    const UINT h_code = scroll_code & 0xffU;
    const UINT v_code = (scroll_code >> 8) & 0xffU;
    if (h_code != 0xffU) {
        for (int other_col = 0; other_col < splitter.col_count; ++other_col) {
            if (other_col != col) {
                handled = splitter_scroll_pane(
                    SplitterGetPane(splitter, row, other_col), scroll_code,
                    position, do_scroll) || handled;
            }
        }
    }
    if (v_code != 0xffU) {
        for (int other_row = 0; other_row < splitter.row_count; ++other_row) {
            if (other_row != row) {
                handled = splitter_scroll_pane(
                    SplitterGetPane(splitter, other_row, col), scroll_code,
                    position, do_scroll) || handled;
            }
        }
    }
    return handled;
}

bool SplitterOnScrollBy(MfcSplitterWndCompat& splitter, MfcCWndCompat& pane,
    SIZE scroll_size, bool do_scroll) {
    int row = -1;
    int col = -1;
    if (!SplitterIsChildPane(splitter, pane, &row, &col)) {
        return false;
    }

    bool handled = splitter_scroll_pane_by(&pane, scroll_size, do_scroll);
    if (scroll_size.cx != 0) {
        for (int other_col = 0; other_col < splitter.col_count; ++other_col) {
            if (other_col != col) {
                handled = splitter_scroll_pane_by(
                    SplitterGetPane(splitter, row, other_col),
                    SIZE{scroll_size.cx, 0}, do_scroll) || handled;
            }
        }
    }
    if (scroll_size.cy != 0) {
        for (int other_row = 0; other_row < splitter.row_count; ++other_row) {
            if (other_row != row) {
                handled = splitter_scroll_pane_by(
                    SplitterGetPane(splitter, other_row, col),
                    SIZE{0, scroll_size.cy}, do_scroll) || handled;
            }
        }
    }
    return handled;
}

bool SplitterCanActivateNext(MfcSplitterWndCompat& splitter) {
    if (splitter.row_count < 1 || splitter.col_count < 1 ||
        splitter.row_count * splitter.col_count < 2) {
        return false;
    }
    return SplitterGetActivePane(splitter, nullptr, nullptr) != nullptr;
}

void SplitterActivateNext(MfcSplitterWndCompat& splitter, bool previous) {
    int row = -1;
    int col = -1;
    if (SplitterGetActivePane(splitter, &row, &col) == nullptr) {
        AfxTraceOutput("Warning: Cannot go to next splitter pane.\n");
        return;
    }
    int index = row * splitter.col_count + col;
    const int count = splitter.row_count * splitter.col_count;
    index += previous ? -1 : 1;
    if (index < 0) {
        index = count - 1;
    } else if (index >= count) {
        index = 0;
    }
    SplitterActivatePane(splitter, index / splitter.col_count,
        index % splitter.col_count, nullptr);
}

void SplitterActivatePane(MfcSplitterWndCompat& splitter, int row, int col,
    MfcCWndCompat* pane) {
    if (pane == nullptr) {
        pane = SplitterGetPane(splitter, row, col);
    }
    if (pane == nullptr) {
        return;
    }
    if (!ObjectIsKindOfRuntimeClass(pane, GetViewRuntimeClass())) {
        AfxTraceOutput("Warning: Next splitter pane is not a CView.\n");
        CWndSetFocus(*pane);
        return;
    }

    MfcFrameWndCompat* parent_frame = CWndGetParentFrameCompat(splitter);
    if (parent_frame == nullptr) {
        CWndSetFocus(*pane);
        return;
    }
    FrameWndSetActiveView(*parent_frame, static_cast<MfcViewCompat*>(pane),
        true);
}

MfcCWndCompat* SplitterGetActivePane(MfcSplitterWndCompat& splitter,
    int* row, int* col) {
    if (row != nullptr) {
        *row = -1;
    }
    if (col != nullptr) {
        *col = -1;
    }
    if (!splitter_valid_window(splitter)) {
        return nullptr;
    }

    MfcCWndCompat* pane = nullptr;
    MfcFrameWndCompat* parent_frame = CWndGetParentFrameCompat(splitter);
    if (parent_frame != nullptr) {
        pane = FrameWndGetActiveView(*parent_frame);
    }
    if (pane == nullptr) {
        pane = CWndFromHandle(GetFocus());
    }
    if (pane != nullptr && SplitterIsChildPane(splitter, *pane, row, col)) {
        return pane;
    }
    return nullptr;
}

void SplitterAssertValid(MfcSplitterWndCompat& splitter) {
    CWndAssertValid(splitter);
    if (splitter.max_rows < 1 || splitter.max_cols < 1 ||
        splitter.row_count < 0 || splitter.col_count < 0 ||
        splitter.row_count > splitter.max_rows ||
        splitter.col_count > splitter.max_cols) {
        AfxTraceOutput("Warning: invalid CSplitterWnd row/column counts.\n");
    }
}

void SplitterDump(const MfcSplitterWndCompat& splitter) {
    CWndDump(splitter);
    AfxTraceOutput("m_nMaxRows = %d\n", splitter.max_rows);
    AfxTraceOutput("m_nMaxCols = %d\n", splitter.max_cols);
    AfxTraceOutput("m_nRows = %d\n", splitter.row_count);
    AfxTraceOutput("m_nCols = %d\n", splitter.col_count);
    AfxTraceOutput("m_bHasHScroll = %d\n", splitter.has_h_scroll ? 1 : 0);
    AfxTraceOutput("m_bHasVScroll = %d\n", splitter.has_v_scroll ? 1 : 0);
    AfxTraceOutput("m_cxSplitter = %d\n", splitter.cx_splitter);
    AfxTraceOutput("m_cySplitter = %d\n", splitter.cy_splitter);
    if (splitter.tracking) {
        AfxTraceOutput("TRACKING m_htTrack = %d\n", splitter.hit_track);
        AfxTraceOutput("m_rectLimit = (%ld,%ld,%ld,%ld)\n",
            splitter.rect_limit.left, splitter.rect_limit.top,
            splitter.rect_limit.right, splitter.rect_limit.bottom);
        AfxTraceOutput("m_ptTrackOffset = (%ld,%ld)\n",
            splitter.track_offset.x, splitter.track_offset.y);
        AfxTraceOutput("m_rectTracker = (%ld,%ld,%ld,%ld)\n",
            splitter.rect_tracker.left, splitter.rect_tracker.top,
            splitter.rect_tracker.right, splitter.rect_tracker.bottom);
        if (splitter.tracking2) {
            AfxTraceOutput("m_rectTracker2 = (%ld,%ld,%ld,%ld)\n",
                splitter.rect_tracker2.left, splitter.rect_tracker2.top,
                splitter.rect_tracker2.right, splitter.rect_tracker2.bottom);
        }
    }
}

namespace {

constexpr UINT kControlBarDelayShowTimer = 0xe000;
constexpr UINT kControlBarDelayHideTimer = 0xe001;
constexpr UINT kMfcSetMessageString = 0x0362;
constexpr UINT kMfcPopMessageString = 0x0375;
constexpr UINT kMfcSizeParentMessage = 0x0361;
constexpr unsigned kControlBarDelayHide = 0x0001;
constexpr unsigned kControlBarDelayShow = 0x0002;
constexpr unsigned kControlBarStatusTextActive = 0x0008;
constexpr DWORD kControlBarToolTips = 0x0010;
constexpr DWORD kControlBarAlignTop = 0x1000;
constexpr DWORD kControlBarAlignBottom = 0x2000;
constexpr DWORD kControlBarAlignLeft = 0x4000;
constexpr DWORD kControlBarAlignRight = 0x8000;
constexpr DWORD kControlBarAlignAny = 0xf000;

MfcCWndCompat* control_bar_owner(MfcControlBarCompat& bar) {
    if (bar.owner_frame != nullptr) {
        return bar.owner_frame;
    }
    if (bar.window == nullptr) {
        return nullptr;
    }
    HWND owner = GetWindow(bar.window, GW_OWNER);
    if (owner == nullptr) {
        owner = GetParent(bar.window);
    }
    return owner == nullptr ? nullptr : CWndFromHandle(owner);
}

void control_bar_fill_client(MfcControlBarCompat& bar, HDC dc) {
    if (bar.window == nullptr || dc == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(bar.window, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
}

} // namespace

MfcRuntimeClassCompat* GetControlBarRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CControlBar", static_cast<int>(sizeof(MfcControlBarCompat)), 0xffff,
        +[]() -> void* {
            auto* bar = new MfcControlBarCompat();
            ConstructControlBar(*bar);
            return bar;
        },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcControlBarCompat& ConstructControlBar(MfcControlBarCompat& bar) {
    ConstructCWnd(bar);
    bar.runtime_class = GetControlBarRuntimeClass();
    bar.auto_delete = false;
    bar.cx_left_border = 0;
    bar.cx_right_border = 0;
    bar.cy_top_border = 0;
    bar.cy_bottom_border = 0;
    bar.cx_default_gap = 0;
    bar.count = 0;
    bar.item_data = nullptr;
    bar.bar_style = 0;
    bar.dock_style = 0;
    bar.state_flags = 0;
    bar.owner_frame = nullptr;
    bar.dock_bar = nullptr;
    bar.dock_context = nullptr;
    bar.owns_dock_context = false;
    bar.status_hit = -1;
    bar.tooltip_enabled = false;
    return bar;
}

void DestroyControlBar(MfcControlBarCompat& bar) {
    ControlBarDestroyWindow(bar);
    if (bar.owner_frame != nullptr) {
        bar.owner_frame = nullptr;
    }
    if (bar.item_data != nullptr && bar.count != 0) {
        std::free(bar.item_data);
    }
    bar.item_data = nullptr;
    bar.count = 0;
    if (bar.owns_dock_context && bar.dock_context != nullptr) {
        DeleteDockContextScalarDtor(bar.dock_context, 1);
    }
    bar.dock_bar = nullptr;
    bar.dock_context = nullptr;
    bar.owns_dock_context = false;
    DestroyCWndCompat(bar);
}

MfcControlBarCompat* DeleteControlBarScalarDtor(MfcControlBarCompat* bar,
    unsigned flags) {
    if (bar == nullptr) {
        return nullptr;
    }
    DestroyControlBar(*bar);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(bar);
    }
    return bar;
}

void ControlBarPostNcDestroy(MfcControlBarCompat* bar) {
    if (bar != nullptr && bar->auto_delete) {
        DeleteControlBarScalarDtor(bar, 1);
    }
}

SIZE ControlBarCalcFixedLayout(MfcControlBarCompat& bar, bool stretch,
    bool horizontal) {
    (void)bar;
    SIZE size{};
    if (stretch) {
        if (horizontal) {
            size.cx = 0x7fff;
        } else {
            size.cy = 0x7fff;
        }
    }
    return size;
}

SIZE ControlBarCalcDynamicLayout(MfcControlBarCompat& bar, int length,
    DWORD mode) {
    (void)length;
    return ControlBarCalcFixedLayout(bar, (mode & 0x02U) != 0,
        (mode & 0x01U) != 0);
}

bool ControlBarDefaultFalse() {
    return false;
}

void ControlBarStartDelayTimer(UINT timer_id, UINT milliseconds) {
    KillTimer(nullptr, kControlBarDelayShowTimer);
    KillTimer(nullptr, kControlBarDelayHideTimer);
    SetTimer(nullptr, timer_id, milliseconds, nullptr);
}

void ControlBarOnTimer(MfcControlBarCompat& bar, UINT timer_id) {
    if (bar.window == nullptr || GetKeyState(VK_LBUTTON) < 0) {
        return;
    }
    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(bar.window, &point);
    TOOLINFOA tool_info{};
    tool_info.cbSize = sizeof(tool_info);
    int hit = static_cast<int>(ControlBarOnToolHitTest(bar, point,
        &tool_info));
    if (hit == static_cast<int>(static_cast<UINT>(-1))) {
        hit = -1;
    }
    if (timer_id == kControlBarDelayShowTimer && hit >= 0) {
        ControlBarSetStatusText(bar, hit);
    } else if (timer_id == kControlBarDelayHideTimer || hit < 0) {
        ControlBarSetStatusText(bar, -1);
    }
}

bool ControlBarSetStatusText(MfcControlBarCompat& bar, int hit) {
    MfcCWndCompat* owner = control_bar_owner(bar);
    if (hit < 0) {
        bar.status_hit = -1;
        if ((bar.state_flags & kControlBarStatusTextActive) != 0 &&
            owner != nullptr && owner->window != nullptr) {
            SendMessageA(owner->window, kMfcPopMessageString,
                kControlBarDelayHideTimer, 0);
        }
        bar.state_flags &= ~kControlBarStatusTextActive;
        KillTimer(nullptr, kControlBarDelayShowTimer);
        return true;
    }
    if ((bar.state_flags & kControlBarStatusTextActive) == 0 ||
        bar.status_hit != hit) {
        bar.status_hit = hit;
        if (owner != nullptr && owner->window != nullptr) {
            SendMessageA(owner->window, kMfcSetMessageString,
                static_cast<WPARAM>(hit), 0);
        }
        bar.state_flags |= kControlBarStatusTextActive;
        ControlBarStartDelayTimer(kControlBarDelayHideTimer, 200);
        return true;
    }
    return false;
}

bool ControlBarPreTranslateMessage(MfcControlBarCompat& bar, MSG& message) {
    if (CWndPreTranslateInput(bar, message)) {
        return true;
    }
    if (bar.window == nullptr || !IsWindow(bar.window)) {
        return false;
    }
    if ((bar.bar_style & kControlBarToolTips) != 0 ||
        message.message == WM_LBUTTONDOWN ||
        message.message == WM_LBUTTONUP) {
        POINT point = message.pt;
        ScreenToClient(bar.window, &point);
        TOOLINFOA tool_info{};
        tool_info.cbSize = sizeof(tool_info);
        int hit = static_cast<int>(ControlBarOnToolHitTest(bar, point,
            &tool_info));
        if (hit == static_cast<int>(static_cast<UINT>(-1))) {
            hit = -1;
        }
        if (message.message == WM_LBUTTONUP || hit < 0) {
            ControlBarSetStatusText(bar, -1);
        } else if (message.message == WM_LBUTTONDOWN ||
            GetKeyState(VK_LBUTTON) < 0) {
            ControlBarSetStatusText(bar, hit);
        } else if (hit != bar.status_hit) {
            ControlBarStartDelayTimer(kControlBarDelayShowTimer, 300);
        }
        bar.status_hit = hit;
    }
    MfcCWndCompat* owner = control_bar_owner(bar);
    if (owner != nullptr && owner->window != nullptr) {
        return AfxPreTranslateMessageFromWindow(owner->window, message);
    }
    return false;
}

LRESULT ControlBarWindowProc(MfcControlBarCompat& bar, UINT message,
    WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_COMPAREITEM:
    case WM_DELETEITEM:
        if (MfcCWndCompat* owner = control_bar_owner(bar)) {
            if (owner->window != nullptr) {
                return SendMessageA(owner->window, message, wparam, lparam);
            }
        }
        break;
    default:
        break;
    }
    return bar.window == nullptr ? 0 :
        DefWindowProcA(bar.window, message, wparam, lparam);
}

int ControlBarOnToolHitTest(MfcControlBarCompat& bar, POINT point,
    TOOLINFOA* tool_info) {
    const UINT hit = CWndOnToolHitTest(bar, point, tool_info);
    if (hit != static_cast<UINT>(-1)) {
        return static_cast<int>(hit + 0x10000U);
    }
    if (bar.window == nullptr) {
        return -1;
    }
    const UINT id = static_cast<UINT>(GetDlgCtrlID(bar.window)) & 0xffffU;
    return id == 0 ? 0 : static_cast<int>(id + 0x50000U);
}

void ControlBarOnWindowPosChanging(MfcControlBarCompat& bar,
    WINDOWPOS& window_pos) {
    if (bar.window != nullptr) {
        DefWindowProcA(bar.window, WM_WINDOWPOSCHANGING, 0,
            reinterpret_cast<LPARAM>(&window_pos));
        if ((window_pos.flags & SWP_NOSIZE) == 0) {
            InvalidateRect(bar.window, nullptr, TRUE);
        }
    }
}

int ControlBarOnCreate(MfcControlBarCompat& bar, CREATESTRUCTA& create) {
    (void)create;
    if ((bar.bar_style & kControlBarToolTips) != 0) {
        bar.tooltip_enabled = CWndEnableToolTips(bar, TRUE);
    }
    HWND parent = bar.window == nullptr ? nullptr : GetParent(bar.window);
    bar.owner_frame = parent == nullptr ? nullptr : CWndFromHandle(parent);
    return 0;
}

void ControlBarOnDestroy(MfcControlBarCompat& bar) {
    ControlBarSetStatusText(bar, -1);
    bar.owner_frame = nullptr;
    CWndDefaultAndReleaseControlSite(bar);
}

void OnDestroy(MfcControlBarCompat& bar) {
    ControlBarOnDestroy(bar);
}

bool ControlBarDestroyWindow(MfcControlBarCompat& bar) {
    return CWndDestroyWindow(bar);
}

int ControlBarOnMouseActivate(MfcControlBarCompat& bar, MfcCWndCompat* top,
    UINT hit_test, UINT message) {
    (void)bar;
    (void)top;
    (void)hit_test;
    (void)message;
    return MA_ACTIVATE;
}

void ControlBarOnPaint(MfcControlBarCompat& bar) {
    if (bar.window == nullptr || !IsWindow(bar.window)) {
        return;
    }
    MfcWindowDCCompat paint_dc{};
    ConstructPaintDC(paint_dc, bar.window);
    control_bar_fill_client(bar, paint_dc.output_dc);
    DestroyPaintDC(paint_dc);
}

void ControlBarEraseNonClient(MfcControlBarCompat& bar) {
    if (bar.window == nullptr || !IsWindow(bar.window)) {
        return;
    }
    MfcWindowDCCompat dc{};
    ConstructWindowDC(dc, bar.window);
    RECT window_rect{};
    GetWindowRect(bar.window, &window_rect);
    OffsetRect(&window_rect, -window_rect.left, -window_rect.top);
    FrameRect(dc.output_dc, &window_rect, GetSysColorBrush(COLOR_BTNFACE));
    DestroyWindowDC(dc);
}

void ControlBarOnLButtonDown(MfcControlBarCompat& bar, UINT flags,
    POINT point) {
    (void)flags;
    if (bar.window == nullptr) {
        return;
    }
    if (ControlBarOnToolHitTest(bar, point, nullptr) < 0) {
        CWndSetFocus(bar);
        return;
    }
    SendMessageA(bar.window, WM_LBUTTONDOWN, flags,
        MAKELPARAM(point.x, point.y));
}

void ControlBarOnLButtonDblClk(MfcControlBarCompat& bar, UINT flags,
    POINT point) {
    (void)flags;
    if (bar.window == nullptr) {
        return;
    }
    if (ControlBarOnToolHitTest(bar, point, nullptr) < 0 &&
        bar.dock_context != nullptr) {
        AfxTraceOutput("CControlBar dock-context double-click requested.\n");
        return;
    }
    SendMessageA(bar.window, WM_LBUTTONDBLCLK, flags,
        MAKELPARAM(point.x, point.y));
}

bool ControlBarDelayShow(MfcControlBarCompat& bar, bool show) {
    const bool visible = (CWndGetStyle(bar) & WS_VISIBLE) != 0;
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (!show && visible) {
        flags |= SWP_HIDEWINDOW;
    } else if (show && !visible) {
        flags |= SWP_SHOWWINDOW;
    } else {
        bar.state_flags &= ~(kControlBarDelayHide | kControlBarDelayShow);
        return false;
    }
    CWndSetWindowPos(bar, nullptr, 0, 0, 0, 0, flags);
    bar.state_flags &= ~(kControlBarDelayHide | kControlBarDelayShow);
    if (MfcCWndCompat* owner = control_bar_owner(bar)) {
        if (owner->window != nullptr) {
            InvalidateRect(owner->window, nullptr, TRUE);
        }
    }
    return true;
}

void ControlBarOnShowWindow(MfcControlBarCompat& bar) {
    ControlBarDelayShow(bar, true);
}

DWORD ControlBarRecalcDelayShow(MfcControlBarCompat& bar, HDWP* hdwp) {
    const DWORD visible = (CWndGetStyle(bar) & WS_VISIBLE);
    DWORD layout_state = (bar.bar_style & 0xff00U) | visible;
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    bool apply = false;
    if ((bar.state_flags & kControlBarDelayHide) != 0 && visible != 0) {
        flags |= SWP_HIDEWINDOW;
        layout_state &= ~WS_VISIBLE;
        apply = true;
    } else if ((bar.state_flags & kControlBarDelayShow) != 0 &&
        visible == 0) {
        flags |= SWP_SHOWWINDOW;
        layout_state |= WS_VISIBLE;
        apply = true;
    }
    if (apply) {
        bar.state_flags &= ~(kControlBarDelayHide | kControlBarDelayShow);
        if (hdwp != nullptr && *hdwp != nullptr && bar.window != nullptr) {
            *hdwp = DeferWindowPos(*hdwp, bar.window, nullptr, 0, 0, 0, 0,
                flags);
        } else {
            CWndSetWindowPos(bar, nullptr, 0, 0, 0, 0, flags);
        }
    }
    return layout_state;
}

LRESULT ControlBarOnSizeParent(MfcControlBarCompat& bar, UINT message,
    MfcSizeParentParamsCompat& layout) {
    (void)message;
    DWORD state = ControlBarRecalcDelayShow(bar, &layout.hdwp);
    if ((state & WS_VISIBLE) == 0 || (state & kControlBarAlignAny) == 0) {
        return 0;
    }

    const bool horizontal = (state & (kControlBarAlignTop |
        kControlBarAlignBottom)) != 0;
    SIZE size = ControlBarCalcDynamicLayout(bar, -1,
        horizontal ? 0x01U : 0x00U);
    size.cx = std::max<LONG>(0, size.cx);
    size.cy = std::max<LONG>(0, size.cy);
    RECT target = layout.rect;

    if ((state & kControlBarAlignTop) != 0) {
        target.bottom = target.top + size.cy;
        layout.rect.top += size.cy;
        layout.size_total.cy += size.cy;
        layout.size_total.cx = std::max(layout.size_total.cx, size.cx);
    } else if ((state & kControlBarAlignBottom) != 0) {
        target.top = target.bottom - size.cy;
        layout.rect.bottom -= size.cy;
        layout.size_total.cy += size.cy;
        layout.size_total.cx = std::max(layout.size_total.cx, size.cx);
    } else if ((state & kControlBarAlignLeft) != 0) {
        target.right = target.left + size.cx;
        layout.rect.left += size.cx;
        layout.size_total.cx += size.cx;
        layout.size_total.cy = std::max(layout.size_total.cy, size.cy);
    } else if ((state & kControlBarAlignRight) != 0) {
        target.left = target.right - size.cx;
        layout.rect.right -= size.cx;
        layout.size_total.cx += size.cx;
        layout.size_total.cy = std::max(layout.size_total.cy, size.cy);
    }

    if (bar.window != nullptr) {
        DeferMoveWindow(&layout.hdwp, bar.window, target);
    }
    return 0;
}

void ControlBarSetDelayShow(MfcControlBarCompat& bar, bool show) {
    bar.state_flags &= ~(kControlBarDelayHide | kControlBarDelayShow);
    const bool visible = (CWndGetStyle(bar) & WS_VISIBLE) != 0;
    if (show && !visible) {
        bar.state_flags |= kControlBarDelayShow;
    } else if (!show && visible) {
        bar.state_flags |= kControlBarDelayHide;
    }
}

bool ControlBarIsVisible(MfcControlBarCompat& bar) {
    if ((bar.state_flags & kControlBarDelayHide) != 0) {
        return false;
    }
    if ((bar.state_flags & kControlBarDelayShow) != 0) {
        return true;
    }
    return (CWndGetStyle(bar) & WS_VISIBLE) != 0;
}

void ControlBarDoPaint(MfcControlBarCompat& bar, MfcCDCCompat& dc) {
    if (bar.window == nullptr || dc.output_dc == nullptr) {
        return;
    }
    RECT rect{};
    GetClientRect(bar.window, &rect);
    ControlBarDrawBorders(bar, dc, rect);
    ControlBarDrawGripper(bar, dc, rect);
    FillRect(dc.output_dc, &rect, GetSysColorBrush(COLOR_BTNFACE));
}

void ControlBarDrawBorders(MfcControlBarCompat& bar, MfcCDCCompat& dc,
    RECT& rect) {
    if (dc.output_dc == nullptr) {
        return;
    }
    RECT border = rect;
    DrawEdge(dc.output_dc, &border, EDGE_RAISED, BF_RECT);
    rect.left += bar.cx_left_border;
    rect.top += bar.cy_top_border;
    rect.right -= bar.cx_right_border;
    rect.bottom -= bar.cy_bottom_border;
    clamp_rect(rect);
}

void ControlBarDrawGripper(MfcControlBarCompat& bar, MfcCDCCompat& dc,
    RECT& rect) {
    if (dc.output_dc == nullptr) {
        return;
    }
    if ((bar.bar_style & 0x400000U) == 0) {
        return;
    }
    RECT gripper = rect;
    const bool vertical =
        (bar.bar_style & (kControlBarAlignLeft | kControlBarAlignRight)) != 0;
    if (vertical) {
        gripper.right = gripper.left + 3;
        rect.left += 7;
    } else {
        gripper.bottom = gripper.top + 3;
        rect.top += 7;
    }
    DrawEdge(dc.output_dc, &gripper, EDGE_RAISED, BF_RECT);
    clamp_rect(rect);
}

void ControlBarCalcInsideRect(MfcControlBarCompat& bar, RECT& rect,
    bool horizontal) {
    rect.left += bar.cx_left_border;
    rect.top += bar.cy_top_border;
    rect.right -= bar.cx_right_border;
    rect.bottom -= bar.cy_bottom_border;
    if ((bar.bar_style & 0x400000U) != 0) {
        if (horizontal) {
            rect.top += 7;
        } else {
            rect.left += 7;
        }
    }
    clamp_rect(rect);
}

DWORD ControlBarGetDockStyle(const MfcControlBarCompat& bar) {
    return bar.dock_style;
}

DWORD ControlBarGetBarStyle(const MfcControlBarCompat& bar) {
    return bar.bar_style;
}

void ControlBarSetBordersLTRB(MfcControlBarCompat& bar, int left, int top,
    int right, int bottom) {
    bar.cx_left_border = std::max(0, left);
    bar.cy_top_border = std::max(0, top);
    bar.cx_right_border = std::max(0, right);
    bar.cy_bottom_border = std::max(0, bottom);
}

void SetBorders(MfcControlBarCompat& bar, const RECT& borders) {
    ControlBarSetBordersLTRB(bar, borders.left, borders.top, borders.right,
        borders.bottom);
}

void DeflateRect(RECT& rect, const RECT& borders) {
    rect.left += borders.left;
    rect.top += borders.top;
    rect.right -= borders.right;
    rect.bottom -= borders.bottom;
}

void ControlBarAssertValid(MfcControlBarCompat& bar) {
    CWndAssertValid(bar);
    if (bar.count != 0 && bar.item_data == nullptr) {
        AfxTraceOutput("Warning: CControlBar item count without item data.\n");
    }
    if ((bar.bar_style & ~0x40ffffUL) != 0) {
        AfxTraceOutput("Warning: CControlBar has unknown style bits 0x%08lx.\n",
            bar.bar_style);
    }
}

MfcDockContextCompat& ConstructDockContext(MfcDockContextCompat& context,
    MfcControlBarCompat& bar) {
    context.runtime_class = nullptr;
    context.start_point = POINT{};
    context.current_point = POINT{};
    context.last_tracker = RECT{};
    context.last_tracker_cx = 0;
    context.last_tracker_cy = 0;
    context.solid_tracker = false;
    context.drag_rect = RECT{};
    context.drag_rect_vertical = RECT{};
    context.frame_rect = RECT{};
    context.frame_rect_vertical = RECT{};
    context.bar = &bar;
    context.dock_site = control_bar_owner(bar);
    context.over_dock_style = 0;
    context.bar_style = bar.bar_style & kControlBarAlignAny;
    context.flip = false;
    context.force_frame = false;
    context.tracking_dc = nullptr;
    context.resize_hit_test = 0;
    context.dragging = false;
    context.recent_dock_id = 0;
    context.recent_dock_rect = RECT{};
    context.mru_dock_style =
        (bar.bar_style & 0x04U) != 0
            ? (bar.bar_style & (kControlBarAlignTop | kControlBarAlignBottom |
                   0x04U))
            : ((bar.bar_style & (kControlBarAlignLeft | kControlBarAlignRight)) != 0
                   ? kControlBarAlignLeft : kControlBarAlignTop);
    context.mru_float_pos.x = static_cast<LONG>(0x80000000U);
    context.mru_float_pos.y = static_cast<LONG>(0x80000000U);
    context.tracking_loop = false;
    bar.dock_context = &context;
    return context;
}

void DestroyDockContext(MfcDockContextCompat& context) {
    DockContextCancelLoop(context);
    if (context.bar != nullptr && context.bar->dock_context == &context) {
        context.bar->dock_context = nullptr;
        context.bar->owns_dock_context = false;
    }
    context.bar = nullptr;
    context.dock_site = nullptr;
}

MfcDockContextCompat* DeleteDockContextScalarDtor(
    MfcDockContextCompat* context, unsigned flags) {
    if (context == nullptr) {
        return nullptr;
    }
    DestroyDockContext(*context);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(context);
    }
    return context;
}

namespace {

RECT dock_context_bar_rect(const MfcDockContextCompat& context, POINT point) {
    RECT rect{};
    if (context.bar != nullptr && context.bar->window != nullptr &&
        IsWindow(context.bar->window)) {
        GetWindowRect(context.bar->window, &rect);
        if (rect_width(rect) > 0 && rect_height(rect) > 0) {
            return rect;
        }
    }
    SIZE size = context.bar == nullptr ? SIZE{120, 28} :
        ControlBarCalcDynamicLayout(*context.bar, -1, 0);
    if (size.cx <= 0) {
        size.cx = 120;
    }
    if (size.cy <= 0) {
        size.cy = 28;
    }
    rect.left = point.x - size.cx / 2;
    rect.top = point.y - size.cy / 2;
    rect.right = rect.left + size.cx;
    rect.bottom = rect.top + size.cy;
    return rect;
}

RECT dock_context_offset_rect(RECT rect, POINT from, POINT to) {
    OffsetRect(&rect, to.x - from.x, to.y - from.y);
    return rect;
}

RECT dock_context_active_rect(const MfcDockContextCompat& context) {
    if ((context.over_dock_style & (kControlBarAlignLeft | kControlBarAlignRight)) != 0) {
        return context.drag_rect_vertical;
    }
    if ((context.over_dock_style & (kControlBarAlignTop | kControlBarAlignBottom)) != 0) {
        return context.drag_rect;
    }
    return context.force_frame || context.flip
        ? context.frame_rect_vertical : context.frame_rect;
}

void dock_context_apply_tracker(MfcDockContextCompat& context, const RECT& rect) {
    if (context.tracking_dc == nullptr) {
        return;
    }
    RECT draw = rect;
    clamp_rect(draw);
    DrawFocusRect(context.tracking_dc, &draw);
    context.last_tracker = draw;
    context.last_tracker_cx = rect_width(draw);
    context.last_tracker_cy = rect_height(draw);
}

} // namespace

void DockContextInitLoop(MfcDockContextCompat& context) {
    MSG paint{};
    while (PeekMessageA(&paint, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) {
        DispatchMessageA(&paint);
    }
    if (context.bar != nullptr) {
        context.bar_style = context.bar->bar_style & kControlBarAlignAny;
        if (context.bar_style == 0) {
            context.bar_style = kControlBarAlignTop;
        }
    }
    context.over_dock_style = 0;
    context.flip = false;
    context.force_frame = false;
    context.last_tracker = RECT{};
    context.last_tracker_cx = 0;
    context.last_tracker_cy = 0;
    context.solid_tracker = false;
    context.tracking_dc = GetDC(GetDesktopWindow());
    context.tracking_loop = true;
    if (context.bar != nullptr && context.bar->window != nullptr) {
        SetCapture(context.bar->window);
    }
}

void DockContextCancelLoop(MfcDockContextCompat& context) {
    DockContextDrawFocusRect(context, true);
    ReleaseCapture();
    if (context.tracking_dc != nullptr) {
        ReleaseDC(GetDesktopWindow(), context.tracking_dc);
        context.tracking_dc = nullptr;
    }
    context.tracking_loop = false;
}

void DockContextDrawFocusRect(MfcDockContextCompat& context, bool remove) {
    if (context.tracking_dc == nullptr) {
        return;
    }
    if (remove && rect_width(context.last_tracker) > 0 &&
        rect_height(context.last_tracker) > 0) {
        RECT old = context.last_tracker;
        DrawFocusRect(context.tracking_dc, &old);
        context.last_tracker = RECT{};
        context.last_tracker_cx = 0;
        context.last_tracker_cy = 0;
        return;
    }
    if (remove) {
        return;
    }
    RECT active = dock_context_active_rect(context);
    dock_context_apply_tracker(context, active);
}

DWORD DockContextCanDock(MfcDockContextCompat& context) {
    if (context.force_frame) {
        return 0;
    }
    DWORD allowed = context.bar_style & kControlBarAlignAny;
    if (allowed == 0) {
        return 0;
    }
    const bool prefer_vertical = context.flip;
    DWORD preferred = prefer_vertical
        ? (allowed & (kControlBarAlignLeft | kControlBarAlignRight))
        : (allowed & (kControlBarAlignTop | kControlBarAlignBottom));
    if (preferred != 0) {
        return preferred;
    }
    return allowed;
}

MfcCWndCompat* DockContextGetDockBar(MfcDockContextCompat& context,
    DWORD dock_style) {
    if (dock_style == 0) {
        return nullptr;
    }
    return context.dock_site;
}

void DockContextMove(MfcDockContextCompat& context, POINT point) {
    POINT previous = context.current_point;
    if (previous.x == 0 && previous.y == 0) {
        previous = context.start_point;
    }
    context.drag_rect = dock_context_offset_rect(context.drag_rect, previous, point);
    context.drag_rect_vertical =
        dock_context_offset_rect(context.drag_rect_vertical, previous, point);
    context.frame_rect = dock_context_offset_rect(context.frame_rect, previous, point);
    context.frame_rect_vertical =
        dock_context_offset_rect(context.frame_rect_vertical, previous, point);
    context.current_point = point;
    context.over_dock_style = context.force_frame ? 0 : DockContextCanDock(context);
    DockContextDrawFocusRect(context, true);
    DockContextDrawFocusRect(context, false);
}

void DockContextSetKeyState(MfcDockContextCompat& context, bool& state,
    bool down) {
    if (state == down) {
        return;
    }
    state = down;
    context.over_dock_style = context.force_frame ? 0 : DockContextCanDock(context);
    DockContextDrawFocusRect(context, true);
    DockContextDrawFocusRect(context, false);
}

void DockContextOnKey(MfcDockContextCompat& context, int key, bool down) {
    if (key == VK_CONTROL) {
        DockContextSetKeyState(context, context.force_frame, down);
    } else if (key == VK_SHIFT) {
        DockContextSetKeyState(context, context.flip, down);
    }
}

void DockContextStartDrag(MfcDockContextCompat& context, POINT point) {
    if (context.bar == nullptr) {
        return;
    }
    context.dragging = true;
    DockContextInitLoop(context);
    context.start_point = point;
    context.current_point = point;
    RECT base = dock_context_bar_rect(context, point);
    context.drag_rect = base;
    context.drag_rect_vertical = base;
    context.frame_rect = base;
    InflateRect(&context.frame_rect, GetSystemMetrics(SM_CXFRAME),
        GetSystemMetrics(SM_CYFRAME));
    context.frame_rect_vertical = context.frame_rect;
    context.over_dock_style = DockContextCanDock(context);
    DockContextDrawFocusRect(context, false);
    DockContextTrack(context);
}

void DockContextEndDrag(MfcDockContextCompat& context) {
    DockContextCancelLoop(context);
    if (context.bar == nullptr || context.bar->window == nullptr) {
        return;
    }
    RECT rect = dock_context_active_rect(context);
    if (context.over_dock_style == 0) {
        context.mru_float_pos.x = rect.left;
        context.mru_float_pos.y = rect.top;
        CWndSetWindowPos(*context.bar, nullptr, rect.left, rect.top,
            rect_width(rect), rect_height(rect),
            SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        context.mru_dock_style = context.over_dock_style;
        context.recent_dock_rect = rect;
        CWndSetWindowPos(*context.bar, nullptr, rect.left, rect.top,
            rect_width(rect), rect_height(rect),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void DockContextStartResize(MfcDockContextCompat& context, int hit_test,
    POINT point) {
    if (context.bar == nullptr) {
        return;
    }
    context.dragging = false;
    context.resize_hit_test = hit_test;
    DockContextInitLoop(context);
    context.start_point = point;
    context.current_point = point;
    RECT base = dock_context_bar_rect(context, point);
    context.drag_rect = base;
    context.drag_rect_vertical = base;
    context.frame_rect = base;
    context.frame_rect_vertical = base;
    context.over_dock_style = 0;
    DockContextStretch(context, point);
    DockContextTrack(context);
}

void DockContextStretch(MfcDockContextCompat& context, POINT point) {
    RECT rect = context.drag_rect;
    const int dx = point.x - context.current_point.x;
    const int dy = point.y - context.current_point.y;
    switch (context.resize_hit_test) {
    case HTLEFT:
    case HTTOPLEFT:
    case HTBOTTOMLEFT:
        rect.left += dx;
        break;
    case HTRIGHT:
    case HTTOPRIGHT:
    case HTBOTTOMRIGHT:
        rect.right += dx;
        break;
    default:
        break;
    }
    switch (context.resize_hit_test) {
    case HTTOP:
    case HTTOPLEFT:
    case HTTOPRIGHT:
        rect.top += dy;
        break;
    case HTBOTTOM:
    case HTBOTTOMLEFT:
    case HTBOTTOMRIGHT:
        rect.bottom += dy;
        break;
    default:
        break;
    }
    if (rect_width(rect) < 16) {
        rect.right = rect.left + 16;
    }
    if (rect_height(rect) < 16) {
        rect.bottom = rect.top + 16;
    }
    context.current_point = point;
    context.drag_rect = rect;
    context.drag_rect_vertical = rect;
    context.frame_rect = rect;
    context.frame_rect_vertical = rect;
    DockContextDrawFocusRect(context, true);
    DockContextDrawFocusRect(context, false);
}

void DockContextEndResize(MfcDockContextCompat& context) {
    DockContextCancelLoop(context);
    if (context.bar == nullptr || context.bar->window == nullptr) {
        return;
    }
    RECT rect = context.drag_rect;
    CWndSetWindowPos(*context.bar, nullptr, rect.left, rect.top,
        rect_width(rect), rect_height(rect), SWP_NOZORDER | SWP_NOACTIVATE);
}

void DockContextToggleDocking(MfcDockContextCompat& context) {
    if (context.bar == nullptr) {
        return;
    }
    if (context.mru_float_pos.x < 0 || context.mru_float_pos.y < 0) {
        RECT rect = dock_context_bar_rect(context, context.current_point);
        context.mru_float_pos.x = rect.left;
        context.mru_float_pos.y = rect.top;
    }
    context.over_dock_style = context.over_dock_style == 0
        ? context.mru_dock_style : 0;
    DockContextEndDrag(context);
}

bool DockContextTrack(MfcDockContextCompat& context) {
    if (context.bar == nullptr || context.bar->window == nullptr) {
        DockContextCancelLoop(context);
        return false;
    }
    MSG message{};
    while (GetCapture() == context.bar->window) {
        if (GetMessageA(&message, nullptr, 0, 0) == 0) {
            PostQuitMessage(static_cast<int>(message.wParam));
            DockContextCancelLoop(context);
            return false;
        }
        switch (message.message) {
        case WM_MOUSEMOVE:
            if (context.dragging) {
                DockContextMove(context, message.pt);
            } else {
                DockContextStretch(context, message.pt);
            }
            break;
        case WM_LBUTTONUP:
            if (context.dragging) {
                DockContextEndDrag(context);
            } else {
                DockContextEndResize(context);
            }
            return true;
        case WM_RBUTTONDOWN:
        case WM_CANCELMODE:
            DockContextCancelLoop(context);
            return false;
        case WM_KEYDOWN:
            DockContextOnKey(context, static_cast<int>(message.wParam), true);
            if (message.wParam == VK_ESCAPE) {
                DockContextCancelLoop(context);
                return false;
            }
            break;
        case WM_KEYUP:
            DockContextOnKey(context, static_cast<int>(message.wParam), false);
            break;
        default:
            DispatchMessageA(&message);
            break;
        }
    }
    DockContextCancelLoop(context);
    return false;
}

namespace {

constexpr int kDockBarGapX = 2;
constexpr int kDockBarGapY = 2;

UINT control_bar_window_id(const MfcControlBarCompat& bar) {
    if (bar.window == nullptr || !IsWindow(bar.window)) {
        return 0;
    }
    return static_cast<UINT>(GetDlgCtrlID(bar.window)) & 0xffffU;
}

void dock_bar_resize_ids(MfcDockBarCompat& dock_bar) {
    if (dock_bar.bar_ids.size() < dock_bar.bars.size()) {
        dock_bar.bar_ids.resize(dock_bar.bars.size(), 0);
    } else if (dock_bar.bars.size() < dock_bar.bar_ids.size()) {
        dock_bar.bars.resize(dock_bar.bar_ids.size(), nullptr);
    }
}

bool dock_bar_is_separator(const MfcDockBarCompat& dock_bar, std::size_t index) {
    return index < dock_bar.bars.size() && index < dock_bar.bar_ids.size() &&
        dock_bar.bars[index] == nullptr && dock_bar.bar_ids[index] == 0;
}

void dock_bar_normalize(MfcDockBarCompat& dock_bar) {
    dock_bar_resize_ids(dock_bar);
    if (dock_bar.bars.empty() || dock_bar.bars.back() != nullptr ||
        dock_bar.bar_ids.back() != 0) {
        dock_bar.bars.push_back(nullptr);
        dock_bar.bar_ids.push_back(0);
    }
    for (std::size_t index = 1; index < dock_bar.bars.size();) {
        if (dock_bar_is_separator(dock_bar, index - 1) &&
            dock_bar_is_separator(dock_bar, index)) {
            dock_bar.bars.erase(dock_bar.bars.begin() + index);
            dock_bar.bar_ids.erase(dock_bar.bar_ids.begin() + index);
        } else {
            ++index;
        }
    }
    if (dock_bar.bars.empty()) {
        dock_bar.bars.push_back(nullptr);
        dock_bar.bar_ids.push_back(0);
    }
}

std::size_t dock_bar_sentinel_index(MfcDockBarCompat& dock_bar) {
    dock_bar_normalize(dock_bar);
    return dock_bar.bars.size() - 1;
}

int dock_bar_find_pointer(const MfcDockBarCompat& dock_bar,
    const MfcControlBarCompat& bar, int start_after) {
    const std::size_t count =
        std::min(dock_bar.bars.size(), dock_bar.bar_ids.size());
    std::size_t start = start_after < 0 ? 0 :
        static_cast<std::size_t>(start_after + 1);
    for (std::size_t index = start; index < count; ++index) {
        if (dock_bar.bars[index] == &bar) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int dock_bar_find_placeholder(const MfcDockBarCompat& dock_bar, UINT id,
    int start_after) {
    if (id == 0) {
        return -1;
    }
    const std::size_t count =
        std::min(dock_bar.bars.size(), dock_bar.bar_ids.size());
    std::size_t start = start_after < 0 ? 0 :
        static_cast<std::size_t>(start_after + 1);
    for (std::size_t index = start; index < count; ++index) {
        if (dock_bar.bars[index] == nullptr && dock_bar.bar_ids[index] == id) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void dock_bar_erase_at(MfcDockBarCompat& dock_bar, std::size_t index) {
    if (index >= dock_bar.bars.size()) {
        return;
    }
    dock_bar.bars.erase(dock_bar.bars.begin() + index);
    if (index < dock_bar.bar_ids.size()) {
        dock_bar.bar_ids.erase(dock_bar.bar_ids.begin() + index);
    }
    dock_bar_normalize(dock_bar);
}

SIZE dock_bar_child_size(MfcControlBarCompat& bar, bool horizontal) {
    SIZE size = ControlBarCalcDynamicLayout(bar, -1,
        horizontal ? 0x01U : 0x00U);
    if ((size.cx <= 0 || size.cy <= 0) && bar.window != nullptr &&
        IsWindow(bar.window)) {
        RECT rect{};
        GetWindowRect(bar.window, &rect);
        if (size.cx <= 0) {
            size.cx = rect_width(rect);
        }
        if (size.cy <= 0) {
            size.cy = rect_height(rect);
        }
    }
    if (size.cx <= 0) {
        size.cx = 120;
    }
    if (size.cy <= 0) {
        size.cy = 28;
    }
    return size;
}

void dock_bar_flush_line(bool horizontal, int line_main, int line_cross,
    int& total_main, int& total_cross) {
    if (line_main == 0 && line_cross == 0) {
        return;
    }
    total_main = std::max(total_main, line_main);
    if (total_cross != 0) {
        total_cross += horizontal ? kDockBarGapY : kDockBarGapX;
    }
    total_cross += line_cross;
}

void dock_bar_apply_layout(MfcDockBarCompat& dock_bar, bool horizontal) {
    dock_bar_normalize(dock_bar);
    if (dock_bar.layout_suspended || dock_bar.window == nullptr ||
        !IsWindow(dock_bar.window)) {
        return;
    }

    int x = 0;
    int y = 0;
    int line_cross = 0;
    const std::size_t count = dock_bar.bars.size();
    for (std::size_t index = 0; index < count; ++index) {
        MfcControlBarCompat* child = dock_bar.bars[index];
        if (child == nullptr) {
            if (dock_bar.bar_ids[index] == 0 && line_cross != 0) {
                if (horizontal) {
                    x = 0;
                    y += line_cross + kDockBarGapY;
                } else {
                    y = 0;
                    x += line_cross + kDockBarGapX;
                }
                line_cross = 0;
            }
            continue;
        }
        if (!ControlBarIsVisible(*child)) {
            continue;
        }
        SIZE child_size = dock_bar_child_size(*child, horizontal);
        if (child->window != nullptr && IsWindow(child->window)) {
            CWndSetWindowPos(*child, nullptr, x, y, child_size.cx,
                child_size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (horizontal) {
            x += child_size.cx + kDockBarGapX;
            line_cross = std::max(line_cross, static_cast<int>(child_size.cy));
        } else {
            y += child_size.cy + kDockBarGapY;
            line_cross = std::max(line_cross, static_cast<int>(child_size.cx));
        }
    }
}

std::size_t dock_bar_insert_index_for_rect(MfcDockBarCompat& dock_bar,
    const RECT& rect, bool horizontal) {
    dock_bar_normalize(dock_bar);
    const POINT target{(rect.left + rect.right) / 2,
        (rect.top + rect.bottom) / 2};
    const std::size_t sentinel = dock_bar.bars.size() - 1;
    for (std::size_t index = 0; index < sentinel; ++index) {
        MfcControlBarCompat* child = dock_bar.bars[index];
        if (child == nullptr || child->window == nullptr ||
            !IsWindow(child->window)) {
            continue;
        }
        RECT child_rect{};
        GetWindowRect(child->window, &child_rect);
        const LONG mid_x = (child_rect.left + child_rect.right) / 2;
        const LONG mid_y = (child_rect.top + child_rect.bottom) / 2;
        if (horizontal) {
            if (target.y < child_rect.top ||
                (target.y <= child_rect.bottom && target.x < mid_x)) {
                return index;
            }
        } else if (target.x < child_rect.left ||
            (target.x <= child_rect.right && target.y < mid_y)) {
            return index;
        }
    }
    return sentinel;
}

void dock_bar_insert(MfcDockBarCompat& dock_bar, std::size_t index,
    MfcControlBarCompat& bar) {
    dock_bar_normalize(dock_bar);
    index = std::min(index, dock_bar.bars.size() - 1);
    dock_bar.bars.insert(dock_bar.bars.begin() + index, &bar);
    dock_bar.bar_ids.insert(dock_bar.bar_ids.begin() + index,
        control_bar_window_id(bar));
    dock_bar_normalize(dock_bar);
}

} // namespace

MfcRuntimeClassCompat* GetDockBarRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CDockBar", static_cast<int>(sizeof(MfcDockBarCompat)), 0xffff,
        +[]() -> void* {
            auto* dock_bar = new MfcDockBarCompat();
            ConstructDockBar(*dock_bar, false);
            return dock_bar;
        },
        GetControlBarRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetDockBarMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {kMfcSizeParentMessage, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NCPAINT, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

MfcDockBarCompat& ConstructDockBar(MfcDockBarCompat& dock_bar,
    bool floating) {
    ConstructControlBar(dock_bar);
    dock_bar.runtime_class = GetDockBarRuntimeClass();
    dock_bar.floating = floating;
    dock_bar.layout_suspended = false;
    dock_bar.layout_rect = RECT{};
    dock_bar.bars.clear();
    dock_bar.bar_ids.clear();
    dock_bar.bars.push_back(nullptr);
    dock_bar.bar_ids.push_back(0);
    return dock_bar;
}

void DestroyDockBar(MfcDockBarCompat& dock_bar) {
    for (MfcControlBarCompat* bar : dock_bar.bars) {
        if (bar != nullptr && bar->dock_bar == &dock_bar) {
            bar->dock_bar = nullptr;
        }
    }
    dock_bar.bars.clear();
    dock_bar.bar_ids.clear();
    dock_bar.floating = false;
    dock_bar.layout_suspended = false;
    dock_bar.layout_rect = RECT{};
    DestroyControlBar(dock_bar);
}

MfcDockBarCompat* DeleteDockBarScalarDtor(MfcDockBarCompat* dock_bar,
    unsigned flags) {
    if (dock_bar == nullptr) {
        return nullptr;
    }
    DestroyDockBar(*dock_bar);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dock_bar);
    }
    return dock_bar;
}

bool DockBarCreate(MfcDockBarCompat& dock_bar, MfcCWndCompat* parent,
    DWORD style, UINT id) {
    if (parent == nullptr || parent->window == nullptr) {
        CrtDbgReport(2, "bardock.cpp", 0x40, nullptr,
            "CDockBar::Create requires a frame parent");
        return false;
    }
    dock_bar.bar_style = style & 0x0040ffffU;
    dock_bar.owner_frame = parent;
    RECT rect{};
    if (!AfxDeferRegisterClass(2)) {
        CrtDbgReport(2, "bardock.cpp", 0x46, nullptr,
            "failed to register the control-bar window class");
        return false;
    }
    if (!CreateAfxRegisteredWindow(dock_bar, "AfxControlBar42sd", nullptr,
            style, rect, parent->window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr)) {
        return false;
    }
    CWndSetDlgCtrlID(dock_bar, static_cast<LONG>(id));
    return true;
}

bool DockBarDefaultTrue() {
    return true;
}

int DockBarGetDockedCount(const MfcDockBarCompat& dock_bar) {
    int count = 0;
    const std::size_t size =
        std::min(dock_bar.bars.size(), dock_bar.bar_ids.size());
    for (std::size_t index = 0; index < size; ++index) {
        if (dock_bar.bars[index] != nullptr) {
            ++count;
        }
    }
    return count;
}

int DockBarGetVisibleDockedCount(MfcDockBarCompat& dock_bar) {
    int count = 0;
    const std::size_t size =
        std::min(dock_bar.bars.size(), dock_bar.bar_ids.size());
    for (std::size_t index = 0; index < size; ++index) {
        MfcControlBarCompat* bar = dock_bar.bars[index];
        if (bar != nullptr && ControlBarIsVisible(*bar)) {
            ++count;
        }
    }
    return count;
}

void DockBarDockControlBar(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, const RECT* rect) {
    dock_bar_normalize(dock_bar);
    const bool horizontal =
        (dock_bar.bar_style & (kControlBarAlignTop | kControlBarAlignBottom)) != 0;
    if (dock_bar.floating && (bar.dock_style & 0x40U) != 0) {
        dock_bar.bar_style |= 0x40U;
    }
    dock_bar.bar_style &= ~0x06U;
    dock_bar.bar_style |= bar.bar_style & 0x06U;

    if (bar.dock_bar != nullptr) {
        DockBarRemoveControlBar(*bar.dock_bar, bar, -1,
            bar.dock_bar == &dock_bar ? -1 : 0);
    }

    std::size_t index = rect == nullptr ? dock_bar_sentinel_index(dock_bar) :
        dock_bar_insert_index_for_rect(dock_bar, *rect, horizontal);
    dock_bar_insert(dock_bar, index, bar);
    bar.dock_bar = &dock_bar;
    bar.owner_frame = dock_bar.owner_frame;

    if (bar.window != nullptr && dock_bar.window != nullptr &&
        IsWindow(bar.window) && IsWindow(dock_bar.window) &&
        GetParent(bar.window) != dock_bar.window) {
        SetParent(bar.window, dock_bar.window);
    }

    if (rect != nullptr && bar.window != nullptr && IsWindow(bar.window)) {
        RECT target = *rect;
        if (dock_bar.window != nullptr && IsWindow(dock_bar.window)) {
            MapWindowPoints(nullptr, dock_bar.window,
                reinterpret_cast<POINT*>(&target), 2);
        }
        CWndSetWindowPos(bar, nullptr, target.left, target.top,
            rect_width(target), rect_height(target),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    dock_bar_apply_layout(dock_bar, horizontal);
    if (dock_bar.owner_frame != nullptr && dock_bar.owner_frame->window != nullptr) {
        InvalidateRect(dock_bar.owner_frame->window, nullptr, TRUE);
    }
}

void DockBarDockControlBarAtRect(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, const RECT* rect) {
    if (bar.dock_bar == &dock_bar) {
        CrtDbgReport(2, "bardock.cpp", 0xd5, nullptr,
            "control bar is already docked in this dock bar");
        return;
    }

    const UINT id = control_bar_window_id(bar);
    int placeholder = dock_bar_find_placeholder(dock_bar, id, -1);
    if (placeholder < 0) {
        DockBarDockControlBar(dock_bar, bar, rect);
        return;
    }

    if (bar.dock_bar != nullptr) {
        DockBarRemoveControlBar(*bar.dock_bar, bar, -1, 0);
    }
    dock_bar.bars[static_cast<std::size_t>(placeholder)] = &bar;
    dock_bar.bar_ids[static_cast<std::size_t>(placeholder)] = id;
    bar.dock_bar = &dock_bar;
    bar.owner_frame = dock_bar.owner_frame;
    if (bar.window != nullptr && dock_bar.window != nullptr &&
        IsWindow(bar.window) && IsWindow(dock_bar.window) &&
        GetParent(bar.window) != dock_bar.window) {
        SetParent(bar.window, dock_bar.window);
    }
    if (rect != nullptr && bar.window != nullptr && IsWindow(bar.window)) {
        RECT target = *rect;
        if (dock_bar.window != nullptr && IsWindow(dock_bar.window)) {
            MapWindowPoints(nullptr, dock_bar.window,
                reinterpret_cast<POINT*>(&target), 2);
        }
        CWndSetWindowPos(bar, nullptr, target.left, target.top,
            rect_width(target), rect_height(target),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    dock_bar_apply_layout(dock_bar,
        (dock_bar.bar_style & (kControlBarAlignTop | kControlBarAlignBottom)) != 0);
}

void DockBarRemovePlaceHolder(MfcDockBarCompat& dock_bar,
    std::uintptr_t bar_or_id) {
    UINT id = static_cast<UINT>(bar_or_id) & 0xffffU;
    if (bar_or_id > 0xffffU) {
        auto* bar = reinterpret_cast<MfcControlBarCompat*>(bar_or_id);
        int index = dock_bar_find_pointer(dock_bar, *bar, -1);
        if (index >= 0) {
            id = dock_bar.bar_ids[static_cast<std::size_t>(index)];
        }
    }
    int placeholder = dock_bar_find_placeholder(dock_bar, id, -1);
    if (placeholder >= 0) {
        dock_bar_erase_at(dock_bar, static_cast<std::size_t>(placeholder));
    }
}

bool DockBarRemoveControlBar(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, int start_after, int save_place_holder) {
    if (save_place_holder != -1 && save_place_holder != 0 &&
        save_place_holder != 1) {
        CrtDbgReport(2, "bardock.cpp", 0x138, nullptr,
            "invalid dock-bar removal mode");
        return false;
    }
    dock_bar_normalize(dock_bar);
    int index = dock_bar_find_pointer(dock_bar, bar, start_after);
    if (index < 0) {
        return false;
    }

    const UINT id = control_bar_window_id(bar);
    if (save_place_holder == 1) {
        dock_bar.bars[static_cast<std::size_t>(index)] = nullptr;
        dock_bar.bar_ids[static_cast<std::size_t>(index)] = id;
        int duplicate = dock_bar_find_placeholder(dock_bar, id, index);
        while (duplicate >= 0) {
            dock_bar_erase_at(dock_bar, static_cast<std::size_t>(duplicate));
            duplicate = dock_bar_find_placeholder(dock_bar, id, index);
        }
    } else {
        dock_bar_erase_at(dock_bar, static_cast<std::size_t>(index));
        if (save_place_holder != -1) {
            DockBarRemovePlaceHolder(dock_bar, id);
        }
    }

    if (bar.dock_bar == &dock_bar) {
        bar.dock_bar = nullptr;
    }
    const bool horizontal =
        (dock_bar.bar_style & (kControlBarAlignTop | kControlBarAlignBottom)) != 0;
    dock_bar_apply_layout(dock_bar, horizontal);
    if (bar.dock_context != nullptr && DockBarGetDockedCount(dock_bar) == 0 &&
        dock_bar.floating) {
        if (MfcCWndCompat* owner = control_bar_owner(dock_bar)) {
            CWndShowWindow(*owner, SW_HIDE);
        }
    }
    return true;
}

SIZE DockBarCalcFixedLayout(MfcDockBarCompat& dock_bar, bool stretch,
    bool horizontal) {
    dock_bar_normalize(dock_bar);
    SIZE base = ControlBarCalcFixedLayout(dock_bar, stretch, horizontal);
    int total_main = 0;
    int total_cross = 0;
    int line_main = 0;
    int line_cross = 0;
    const std::size_t count = dock_bar.bars.size();
    for (std::size_t index = 0; index < count; ++index) {
        MfcControlBarCompat* child = dock_bar.bars[index];
        if (child == nullptr) {
            if (dock_bar.bar_ids[index] == 0) {
                dock_bar_flush_line(horizontal, line_main, line_cross,
                    total_main, total_cross);
                line_main = 0;
                line_cross = 0;
            }
            continue;
        }
        if (!ControlBarIsVisible(*child)) {
            continue;
        }
        SIZE child_size = dock_bar_child_size(*child, horizontal);
        if (horizontal) {
            if (line_main != 0) {
                line_main += kDockBarGapX;
            }
            line_main += child_size.cx;
            line_cross = std::max(line_cross, static_cast<int>(child_size.cy));
        } else {
            if (line_main != 0) {
                line_main += kDockBarGapY;
            }
            line_main += child_size.cy;
            line_cross = std::max(line_cross, static_cast<int>(child_size.cx));
        }
    }
    dock_bar_flush_line(horizontal, line_main, line_cross, total_main,
        total_cross);

    SIZE result{};
    if (horizontal) {
        result.cx = std::max<LONG>(base.cx, total_main);
        result.cy = std::max<LONG>(base.cy, total_cross);
        if (stretch) {
            result.cx = 0x7fff;
        }
    } else {
        result.cx = std::max<LONG>(base.cx, total_cross);
        result.cy = std::max<LONG>(base.cy, total_main);
        if (stretch) {
            result.cy = 0x7fff;
        }
    }
    dock_bar_apply_layout(dock_bar, horizontal);
    return result;
}

void DockBarCalcInsideRect(MfcDockBarCompat& dock_bar, RECT& rect,
    bool horizontal) {
    ControlBarCalcInsideRect(dock_bar, rect, horizontal);
}

void DockBarOnNcPaint(MfcDockBarCompat& dock_bar) {
    ControlBarEraseNonClient(dock_bar);
}

void DockBarDoPaint(MfcDockBarCompat& dock_bar, MfcCDCCompat& dc) {
    (void)dock_bar;
    (void)dc;
}

void DockBarOnNcCalcSize(MfcDockBarCompat& dock_bar) {
    (void)dock_bar;
}

void DockBarOnPaint(MfcDockBarCompat& dock_bar) {
    if (dock_bar.window == nullptr || !IsWindow(dock_bar.window)) {
        return;
    }
    MfcWindowDCCompat paint_dc{};
    ConstructPaintDC(paint_dc, dock_bar.window);
    if (ControlBarIsVisible(dock_bar) && DockBarDefaultTrue()) {
        DockBarDoPaint(dock_bar, paint_dc);
    }
    DestroyPaintDC(paint_dc);
}

void DockBarOnWindowPosChanging(MfcDockBarCompat& dock_bar,
    WINDOWPOS& window_pos) {
    DWORD old_style = dock_bar.bar_style;
    dock_bar.bar_style &= 0xfffff0ffU;
    ControlBarOnWindowPosChanging(dock_bar, window_pos);
    dock_bar.bar_style = old_style;
}

int DockBarFindBar(const MfcDockBarCompat& dock_bar,
    std::uintptr_t raw_entry, int skip_index) {
    const std::size_t size =
        std::min(dock_bar.bars.size(), dock_bar.bar_ids.size());
    for (std::size_t index = 0; index < size; ++index) {
        if (static_cast<int>(index) == skip_index) {
            continue;
        }
        if (raw_entry > 0xffffU) {
            if (dock_bar.bars[index] ==
                reinterpret_cast<const MfcControlBarCompat*>(raw_entry)) {
                return static_cast<int>(index);
            }
        } else if (dock_bar.bar_ids[index] ==
            (static_cast<UINT>(raw_entry) & 0xffffU)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void DockBarOnUpdateCmdUI(MfcDockBarCompat& dock_bar,
    MfcCWndCompat* target, bool disable_if_no_handler) {
    for (MfcControlBarCompat* bar : dock_bar.bars) {
        if (bar == nullptr) {
            continue;
        }
        if (ObjectIsKindOfRuntimeClass(bar, GetToolBarRuntimeClass())) {
            ToolBarOnUpdateCmdUI(*static_cast<MfcToolBarCompat*>(bar),
                nullptr, disable_if_no_handler);
        } else if (ObjectIsKindOfRuntimeClass(bar, GetDialogBarRuntimeClass())) {
            DialogBarOnUpdateCmdUI(*static_cast<MfcDialogBarCompat*>(bar),
                target, disable_if_no_handler);
        } else if (target != nullptr) {
            CWndUpdateDialogControls(*bar, target, disable_if_no_handler);
        }
    }
}

MfcControlBarCompat* DockBarGetDockedControlBar(
    const MfcDockBarCompat& dock_bar, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= dock_bar.bars.size()) {
        return nullptr;
    }
    return dock_bar.bars[static_cast<std::size_t>(index)];
}

int DockBarInsertBarAtRect(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, const RECT& rect) {
    const bool horizontal =
        (dock_bar.bar_style & (kControlBarAlignTop | kControlBarAlignBottom)) != 0;
    std::size_t index = dock_bar_insert_index_for_rect(dock_bar, rect,
        horizontal);
    dock_bar_insert(dock_bar, index, bar);
    bar.dock_bar = &dock_bar;
    return static_cast<int>(index);
}

void DockBarAssertValid(MfcDockBarCompat& dock_bar) {
    ControlBarAssertValid(dock_bar);
    dock_bar_normalize(dock_bar);
    if (dock_bar.bars.empty()) {
        CrtDbgReport(2, "bardock.cpp", 0x2c3, nullptr,
            "CDockBar array must not be empty");
        return;
    }
    if (!dock_bar_is_separator(dock_bar, 0)) {
        CrtDbgReport(2, "bardock.cpp", 0x2c4, nullptr,
            "CDockBar array must start with a null separator");
    }
    if (!dock_bar_is_separator(dock_bar, dock_bar.bars.size() - 1)) {
        CrtDbgReport(2, "bardock.cpp", 0x2c5, nullptr,
            "CDockBar array must end with a null separator");
    }
}

void DockBarDump(MfcDockBarCompat& dock_bar, MfcDumpContext& dump_context) {
    (void)dump_context;
    CWndDump(dock_bar);
    AfxTraceOutput("m_arrBars = %zu entries\n", dock_bar.bars.size());
    for (std::size_t index = 0; index < dock_bar.bars.size(); ++index) {
        AfxTraceOutput("  [%zu] bar=%p id=%u\n", index,
            dock_bar.bars[index], index < dock_bar.bar_ids.size()
                ? dock_bar.bar_ids[index] : 0U);
    }
    AfxTraceOutput("m_bFloating = %d\n", dock_bar.floating ? 1 : 0);
}

void ControlBarEnableDocking(MfcControlBarCompat& bar, DWORD dock_style) {
    constexpr DWORD valid_dock_bits = kControlBarAlignAny | 0x40U;
    if ((dock_style & ~valid_dock_bits) != 0) {
        CrtDbgReport(2, "bardock.cpp", 0x327, nullptr,
            "invalid control-bar docking style");
        dock_style &= valid_dock_bits;
    }
    bar.dock_style = dock_style;
    bar.bar_style |= dock_style & kControlBarAlignAny;
    if (bar.owner_frame == nullptr && bar.window != nullptr &&
        IsWindow(bar.window)) {
        HWND parent = GetParent(bar.window);
        bar.owner_frame = parent == nullptr ? nullptr : CWndFromHandle(parent);
    }
    if (bar.dock_context == nullptr) {
        void* storage = MfcDebugNewClientBlock(sizeof(MfcDockContextCompat));
        auto* context = new (storage) MfcDockContextCompat();
        ConstructDockContext(*context, bar);
        bar.owns_dock_context = true;
    }
}

constexpr DWORD kMiniFrameThickFrameStyle = 0x0004;
constexpr DWORD kMiniFrameFourThickFrameStyle = 0x0010;

MfcRuntimeClassCompat* GetMiniFrameRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CMiniFrameWnd", static_cast<int>(sizeof(MfcMiniFrameWndCompat)),
        0xffff,
        +[]() -> void* {
            auto* frame = new MfcMiniFrameWndCompat();
            ConstructMiniFrameWnd(*frame);
            return frame;
        },
        GetFrameWndRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcMiniFrameWndCompat& ConstructMiniFrameWnd(MfcMiniFrameWndCompat& frame) {
    ConstructFrameWnd(frame);
    frame.runtime_class = GetMiniFrameRuntimeClass();
    frame.sys_menu_tracking = false;
    frame.sys_menu_hot = false;
    frame.active_caption = false;
    frame.mini_caption.clear();
    InitializeMiniFrameMetrics();
    return frame;
}

void DestroyMiniFrameWnd(MfcMiniFrameWndCompat& frame) {
    frame.sys_menu_tracking = false;
    frame.sys_menu_hot = false;
    frame.active_caption = false;
    frame.mini_caption.clear();
    DestroyFrameWnd(frame);
}

MfcMiniFrameWndCompat* DeleteMiniFrameScalarDtor(
    MfcMiniFrameWndCompat* frame, unsigned flags) {
    if (frame == nullptr) {
        return nullptr;
    }
    DestroyMiniFrameWnd(*frame);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(frame);
    }
    return frame;
}

MfcFrameWndCompat* FrameWndFromChild(MfcCWndCompat* child) {
    if (child == nullptr) {
        return nullptr;
    }
    if (ObjectIsKindOfRuntimeClass(child, GetFrameWndRuntimeClass())) {
        return static_cast<MfcFrameWndCompat*>(child);
    }
    HWND current = child->window;
    while (current != nullptr) {
        current = GetParent(current);
        MfcCWndCompat* window =
            current == nullptr ? nullptr : CWndFromHandlePermanent(current);
        if (window != nullptr &&
            ObjectIsKindOfRuntimeClass(window, GetFrameWndRuntimeClass())) {
            return static_cast<MfcFrameWndCompat*>(window);
        }
    }
    MfcCWndCompat* main = AfxGetMainWndCompat();
    return main != nullptr &&
        ObjectIsKindOfRuntimeClass(main, GetFrameWndRuntimeClass())
        ? static_cast<MfcFrameWndCompat*>(main) : nullptr;
}

int MiniFrameGetCaptionShowMode(MfcMiniFrameWndCompat& frame) {
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return 0;
    }
    if ((CWndGetStyle(frame) & WS_VISIBLE) == 0) {
        return 0;
    }
    return IsIconic(frame.window) ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE;
}

void CleanupMiniFrameMetrics() {
    DeleteGdiObjectHandle(reinterpret_cast<HGDIOBJ*>(&g_mini_frame_caption_bitmap));
    DeleteGdiObjectHandle(reinterpret_cast<HGDIOBJ*>(&g_mini_frame_caption_font));
}

void InitializeMiniFrameMetricsThunk() {
    RegisterMiniFrameMetricsCleanup();
}

void RegisterMiniFrameMetricsCleanup() {
    if (!g_mini_frame_metrics_cleanup_registered) {
        g_mini_frame_metrics_cleanup_registered = true;
        CrtAtexit(CleanupMiniFrameMetrics);
    }
}

void InitializeMiniFrameMetrics() {
    RegisterMiniFrameMetricsCleanup();
    if (g_mini_frame_caption_bitmap == nullptr) {
        g_mini_frame_caption_bitmap =
            LoadBitmapA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(0x7912));
        if (g_mini_frame_caption_bitmap != nullptr) {
            BITMAP bitmap{};
            if (GetObjectA(g_mini_frame_caption_bitmap, sizeof(bitmap),
                    &bitmap) != 0) {
                g_mini_frame_caption_bitmap_size.cx = bitmap.bmWidth;
                g_mini_frame_caption_bitmap_size.cy = bitmap.bmHeight;
            }
        }
    }
    if (g_mini_frame_caption_font == nullptr) {
        LOGFONTA font{};
        font.lfHeight = -std::max(8,
            static_cast<int>(g_mini_frame_caption_bitmap_size.cy) - 1);
        font.lfCharSet = DEFAULT_CHARSET;
        font.lfWeight = FW_NORMAL;
        lstrcpyA(font.lfFaceName, GetSystemMetrics(SM_DBCSENABLED) == 0
            ? "Small Fonts" : "Terminal");
        g_mini_frame_caption_font = CreateFontIndirectA(&font);
    }
}

bool MiniFrameCreate(MfcMiniFrameWndCompat& frame, const char* class_name,
    const char* window_name, DWORD style, const RECT& rect,
    MfcCWndCompat* parent, UINT id) {
    return MiniFrameCreateEx(frame, 0, class_name, window_name, style, rect,
        parent, id);
}

bool MiniFrameCreateEx(MfcMiniFrameWndCompat& frame, DWORD ex_style,
    const char* class_name, const char* window_name, DWORD style,
    const RECT& rect, MfcCWndCompat* parent, UINT id) {
    if (class_name == nullptr) {
        class_name = AfxRegisterWndClassCompat(CS_DBLCLKS,
            LoadCursorA(nullptr, IDC_ARROW), nullptr, nullptr);
    }
    frame.mini_caption = window_name == nullptr ? "" : window_name;
    HWND parent_hwnd = parent == nullptr ? nullptr : parent->window;
    return CreateWindowExFromRect(frame, ex_style, class_name,
        window_name == nullptr ? "" : window_name, style, rect, parent_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr);
}

bool MiniFrameOnNcCreate(MfcMiniFrameWndCompat& frame, CREATESTRUCTA& create) {
    if (OnNcCreate(frame, &create) == 0) {
        return false;
    }
    frame.mini_caption = create.lpszName == nullptr ? "" : create.lpszName;
    if ((CWndGetStyle(frame) & WS_CHILD) != 0) {
        MfcCWndCompat* top_parent = GetTopLevelParent(frame);
        HWND foreground = GetForegroundWindow();
        bool active = top_parent != nullptr && top_parent->window == foreground;
        if (!active && top_parent != nullptr) {
            MfcCWndCompat* popup = CWndGetLastActivePopupInline(*top_parent);
            active = popup != nullptr && popup->window == foreground &&
                frame.window != nullptr &&
                SendMessageA(frame.window, kMfcFloatingFrameMessage, 0x40, 0) != 0;
        }
        if (frame.window != nullptr) {
            SendMessageA(frame.window, kMfcFloatingFrameMessage,
                active ? 0x04 : 0x08, 0);
        }
        frame.active_caption = active;
    } else {
        frame.active_caption = GetForegroundWindow() == frame.window;
    }
    return true;
}

bool MiniFramePreCreateWindow(MfcMiniFrameWndCompat& frame,
    CREATESTRUCTA& create) {
    if ((create.style &
            (kMiniFrameFourThickFrameStyle | kMiniFrameThickFrameStyle)) != 0) {
        create.style |= WS_THICKFRAME;
    }
    if ((create.style & (WS_CAPTION | WS_SYSMENU)) != 0) {
        create.dwExStyle |= WS_EX_TOOLWINDOW;
    }
    if (!FrameWndPreCreateWindow(frame, create)) {
        return false;
    }
    create.dwExStyle &= ~WS_EX_CLIENTEDGE;
    return true;
}

bool MiniFrameOnNcActivate(MfcMiniFrameWndCompat& frame, BOOL active) {
    if ((CWndGetStyle(frame) & WS_CHILD) == 0) {
        const bool next_active = active != FALSE;
        if (frame.active_caption != next_active) {
            frame.active_caption = next_active;
            if (frame.window != nullptr && IsWindow(frame.window)) {
                SendMessageA(frame.window, WM_NCPAINT, 0, 0);
            }
        }
    } else if ((frame.wnd_flags & kMfcWndFlagKeepMiniActive) != 0) {
        return false;
    }
    return true;
}

void MiniFrameCalcInsideRect(MfcMiniFrameWndCompat& frame, RECT& rect) {
    const DWORD style = static_cast<DWORD>(CWndGetStyle(frame));
    if ((style & (kMiniFrameFourThickFrameStyle |
            kMiniFrameThickFrameStyle | WS_THICKFRAME)) == 0) {
        InflateRect(&rect, -GetSystemMetrics(SM_CXBORDER),
            -GetSystemMetrics(SM_CYBORDER));
    } else {
        InflateRect(&rect, -GetSystemMetrics(SM_CXFRAME),
            -GetSystemMetrics(SM_CYFRAME));
    }
    if ((style & WS_CAPTION) != 0) {
        rect.top += g_mini_frame_caption_bitmap_size.cy;
    }
}

UINT MiniFrameHitTest(MfcMiniFrameWndCompat& frame, POINT point) {
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return HTNOWHERE;
    }
    RECT window_rect{};
    GetWindowRect(frame.window, &window_rect);
    const DWORD style = static_cast<DWORD>(CWndGetStyle(frame));
    if (!PtInRect(&window_rect, point)) {
        return HTNOWHERE;
    }
    RECT inside = window_rect;
    MiniFrameCalcInsideRect(frame, inside);
    if (PtInRect(&inside, point)) {
        return HTCLIENT;
    }
    if ((style & WS_CAPTION) != 0 &&
        point.y < inside.top &&
        point.y >= window_rect.top + GetSystemMetrics(SM_CYFRAME)) {
        if ((style & WS_SYSMENU) != 0 &&
            point.x < window_rect.left + g_mini_frame_caption_bitmap_size.cx +
                GetSystemMetrics(SM_CXFRAME)) {
            return HTSYSMENU;
        }
        return HTCAPTION;
    }
    if ((style & WS_THICKFRAME) == 0) {
        return HTBORDER;
    }
    const int cx = GetSystemMetrics(SM_CXFRAME);
    const int cy = GetSystemMetrics(SM_CYFRAME);
    const bool left = point.x < window_rect.left + cx;
    const bool right = point.x >= window_rect.right - cx;
    const bool top = point.y < window_rect.top + cy;
    const bool bottom = point.y >= window_rect.bottom - cy;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTBORDER;
}

void MiniFrameOnNcLButtonDown(MfcMiniFrameWndCompat& frame, UINT hit_test,
    POINT point) {
    if (hit_test == HTSYSMENU) {
        frame.sys_menu_tracking = true;
        frame.sys_menu_hot = true;
        SetCapture(frame.window);
        MiniFrameInvertSysMenu(frame);
        return;
    }
    DefWindowProcA(frame.window, WM_NCLBUTTONDOWN, hit_test,
        MAKELPARAM(point.x, point.y));
}

void MiniFrameOnNcMouseMove(MfcMiniFrameWndCompat& frame, UINT hit_test,
    POINT point) {
    if (!frame.sys_menu_tracking) {
        DefWindowProcA(frame.window, WM_NCMOUSEMOVE, hit_test,
            MAKELPARAM(point.x, point.y));
        return;
    }
    const bool hot = MiniFrameHitTest(frame, point) == HTSYSMENU;
    if (hot != frame.sys_menu_hot) {
        frame.sys_menu_hot = hot;
        MiniFrameInvertSysMenu(frame);
    }
}

void MiniFrameOnNcLButtonUp(MfcMiniFrameWndCompat& frame, UINT hit_test,
    POINT point) {
    if (!frame.sys_menu_tracking) {
        DefWindowProcA(frame.window, WM_NCLBUTTONUP, hit_test,
            MAKELPARAM(point.x, point.y));
        return;
    }
    ReleaseCapture();
    frame.sys_menu_tracking = false;
    if (MiniFrameHitTest(frame, point) == HTSYSMENU) {
        MiniFrameInvertSysMenu(frame);
        SendMessageA(frame.window, WM_CLOSE, 0, 0);
    }
}

void MiniFrameInvertSysMenu(MfcMiniFrameWndCompat& frame) {
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return;
    }
    HDC dc = GetWindowDC(frame.window);
    if (dc == nullptr) {
        return;
    }
    RECT rect{};
    GetWindowRect(frame.window, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    RECT sys{rect.left + GetSystemMetrics(SM_CXFRAME),
        rect.top + GetSystemMetrics(SM_CYFRAME),
        rect.left + GetSystemMetrics(SM_CXFRAME) +
            g_mini_frame_caption_bitmap_size.cx,
        rect.top + GetSystemMetrics(SM_CYFRAME) +
            g_mini_frame_caption_bitmap_size.cy};
    InvertRect(dc, &sys);
    ReleaseDC(frame.window, dc);
}

void MiniFrameDrawBorder(MfcMiniFrameWndCompat& frame, HDC dc,
    const RECT& rect, int cx, int cy) {
    (void)frame;
    RECT top{rect.left, rect.top, rect.right, rect.top + cy};
    RECT bottom{rect.left, rect.bottom - cy, rect.right, rect.bottom};
    RECT left{rect.left, rect.top, rect.left + cx, rect.bottom};
    RECT right{rect.right - cx, rect.top, rect.right, rect.bottom};
    FillRect(dc, &top, GetSysColorBrush(COLOR_3DFACE));
    FillRect(dc, &bottom, GetSysColorBrush(COLOR_3DSHADOW));
    FillRect(dc, &left, GetSysColorBrush(COLOR_3DFACE));
    FillRect(dc, &right, GetSysColorBrush(COLOR_3DSHADOW));
}

void MiniFrameOnNcPaint(MfcMiniFrameWndCompat& frame) {
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return;
    }
    HDC dc = GetWindowDC(frame.window);
    if (dc == nullptr) {
        return;
    }
    RECT rect{};
    GetWindowRect(frame.window, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    MiniFrameDrawBorder(frame, dc, rect, GetSystemMetrics(SM_CXFRAME),
        GetSystemMetrics(SM_CYFRAME));
    if ((CWndGetStyle(frame) & WS_CAPTION) != 0) {
        RECT caption = rect;
        caption.left += GetSystemMetrics(SM_CXFRAME);
        caption.right -= GetSystemMetrics(SM_CXFRAME);
        caption.top += GetSystemMetrics(SM_CYFRAME);
        caption.bottom = caption.top + g_mini_frame_caption_bitmap_size.cy;
        FillRect(dc, &caption, GetSysColorBrush(frame.active_caption
            ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
        HFONT old_font = g_mini_frame_caption_font == nullptr ? nullptr
            : static_cast<HFONT>(SelectObject(dc, g_mini_frame_caption_font));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(frame.active_caption
            ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
        DrawTextA(dc, frame.mini_caption.c_str(), -1, &caption,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
        if (old_font != nullptr) {
            SelectObject(dc, old_font);
        }
    }
    ReleaseDC(frame.window, dc);
}

void MiniFrameOnSysCommand(MfcMiniFrameWndCompat& frame, UINT command,
    LPARAM lparam) {
    if (frame.window == nullptr) {
        return;
    }
    const DWORD style = static_cast<DWORD>(CWndGetStyle(frame));
    bool route_to_frame = (style & WS_DISABLED) == 0;
    if (!route_to_frame) {
        const bool close_command = (command & 0xfff0U) == SC_CLOSE;
        const bool alt_f4_down =
            GetKeyState(VK_F4) < 0 && GetKeyState(VK_MENU) < 0;
        if (close_command && (!alt_f4_down || (style & WS_CHILD) == 0)) {
            route_to_frame = true;
        } else {
            route_to_frame = !CWndOnSysCommand(frame, command, lparam);
        }
    }
    if (route_to_frame) {
        FrameWndOnSysCommand(frame, command, lparam);
    }
}

void MiniFrameCalcWindowRect(MfcMiniFrameWndCompat& frame, RECT& rect,
    bool include_menu) {
    (void)include_menu;
    MiniFrameAdjustWindowRect(rect, static_cast<DWORD>(CWndGetStyle(frame)));
}

void MiniFrameAdjustWindowRect(RECT& rect, DWORD style) {
    if ((style & (kMiniFrameFourThickFrameStyle |
            kMiniFrameThickFrameStyle | WS_THICKFRAME)) == 0) {
        InflateRect(&rect, GetSystemMetrics(SM_CXBORDER),
            GetSystemMetrics(SM_CYBORDER));
    } else {
        InflateRect(&rect, GetSystemMetrics(SM_CXFRAME),
            GetSystemMetrics(SM_CYFRAME));
    }
    if ((style & WS_CAPTION) != 0) {
        InitializeMiniFrameMetrics();
        rect.top -= g_mini_frame_caption_bitmap_size.cy;
    }
}

void MiniFrameOnGetMinMaxInfo(MfcMiniFrameWndCompat& frame,
    MINMAXINFO& min_max) {
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return;
    }
    DefWindowProcA(frame.window, WM_GETMINMAXINFO, 0,
        reinterpret_cast<LPARAM>(&min_max));
    RECT window_rect{};
    RECT client_rect{};
    GetWindowRect(frame.window, &window_rect);
    GetClientRect(frame.window, &client_rect);
    min_max.ptMinTrackSize.x = RectWidth(window_rect) - RectWidth(client_rect);
    min_max.ptMinTrackSize.y = RectHeight(window_rect) - RectHeight(client_rect);
}

LRESULT MiniFrameOnGetText(MfcMiniFrameWndCompat& frame, int max_count,
    char* buffer) {
    if (buffer == nullptr || max_count <= 0) {
        return 0;
    }
    lstrcpynA(buffer, frame.mini_caption.c_str(), max_count);
    return std::min<int>(max_count - 1,
        static_cast<int>(frame.mini_caption.size()));
}

int MiniFrameOnGetTextLength(MfcMiniFrameWndCompat& frame) {
    return static_cast<int>(frame.mini_caption.size());
}

void MiniFrameOnSetText(MfcMiniFrameWndCompat& frame, const char* text) {
    frame.mini_caption = text == nullptr ? "" : text;
    if (frame.window != nullptr && IsWindow(frame.window)) {
        SendMessageA(frame.window, WM_NCPAINT, 0, 0);
    }
}

bool MiniFrameOnSetTextEpilogue() {
    return true;
}

bool MiniFrameModifyStyleFlags(MfcMiniFrameWndCompat& frame, DWORD flags) {
    if ((flags & 0x03U) == 0x03U) {
        CrtDbgReport(2, "winmini.cpp", 0x2ef, nullptr,
            "CMiniFrameWnd show and hide flags are mutually exclusive");
    }
    if ((flags & 0x30U) == 0x30U) {
        CrtDbgReport(2, "winmini.cpp", 0x2f0, nullptr,
            "CMiniFrameWnd enable and disable flags are mutually exclusive");
    }
    if ((flags & 0x0cU) == 0x0cU) {
        CrtDbgReport(2, "winmini.cpp", 0x2f1, nullptr,
            "CMiniFrameWnd activate and deactivate flags are mutually exclusive");
    }
    if ((flags & 0x03U) != 0) {
        CWndSetWindowPos(frame, nullptr, 0, 0, 0, 0,
            ((flags & 0x01U) != 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) |
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if ((flags & 0x30U) != 0) {
        CWndEnableWindow(frame, (flags & 0x10U) != 0);
    }
    if ((flags & 0x0cU) != 0 && (CWndGetStyle(frame) & WS_CHILD) != 0) {
        CWndModifyStyle(frame, WS_CHILD, 0, 0);
        SendMessageA(frame.window, WM_NCACTIVATE,
            (flags & 0x04U) != 0 ? TRUE : FALSE, 0);
        CWndModifyStyle(frame, 0, WS_CHILD, 0);
    }
    return (CWndGetStyle(frame) & WS_CHILD) != 0 && (flags & 0x40U) != 0;
}

HWND MiniFrameOnQueryCenterWnd(MfcMiniFrameWndCompat& frame) {
    HWND parent = frame.window == nullptr ? nullptr : GetParent(frame.window);
    if (parent == nullptr) {
        return nullptr;
    }
    HWND center = reinterpret_cast<HWND>(SendMessageA(parent, 0x036b, 0, 0));
    return center == nullptr ? parent : center;
}

MfcMiniDockFrameWndCompat* FrameWndCreateFloatingFrame(
    MfcFrameWndCompat& frame, DWORD dock_style) {
    if (frame.floating_frame_class == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0x27, nullptr,
            "floating frame runtime class is not installed");
        frame.floating_frame_class = GetMiniDockFrameRuntimeClass();
    }
    void* object = RuntimeClassCreateObject(*frame.floating_frame_class);
    if (object == nullptr) {
        ThrowMfcMemoryException();
    }
    auto* base = static_cast<MfcObjectCompat*>(object);
    if (!ObjectIsKindOfRuntimeClass(base, GetMiniDockFrameRuntimeClass())) {
        CrtDbgReport(2, "winfrm2.cpp", 0x2b, nullptr,
            "floating frame class must create CMiniDockFrameWnd");
        ThrowMfcResourceException();
    }
    auto* mini = static_cast<MfcMiniDockFrameWndCompat*>(base);
    if (!MiniDockFrameCreate(*mini, &frame, dock_style)) {
        DestroyMiniDockFrameWnd(*mini);
        delete mini;
        ThrowMfcResourceException();
    }
    return mini;
}

void FrameWndEnableDocking(MfcFrameWndCompat& frame, DWORD dock_style) {
    constexpr UINT dock_ids[] = {0xe81b, 0xe81c, 0xe81d, 0xe81e};
    constexpr DWORD dock_styles[] = {
        kControlBarAlignTop, kControlBarAlignLeft,
        kControlBarAlignRight, kControlBarAlignBottom};
    if ((dock_style & ~(kControlBarAlignAny | 0x40U)) != 0) {
        CrtDbgReport(2, "winfrm2.cpp", 0x38, nullptr,
            "invalid frame docking style");
        dock_style &= kControlBarAlignAny | 0x40U;
    }
    frame.floating_frame_class = GetMiniDockFrameRuntimeClass();
    frame.control_bars.erase(std::remove(frame.control_bars.begin(),
        frame.control_bars.end(), nullptr), frame.control_bars.end());
    for (std::size_t index = 0; index < std::size(dock_ids); ++index) {
        if ((dock_style & dock_styles[index]) == 0) {
            continue;
        }
        if (FrameWndGetControlBar(frame, dock_ids[index]) != nullptr) {
            continue;
        }
        auto* dock_bar = new MfcDockBarCompat();
        ConstructDockBar(*dock_bar, false);
        if (!DockBarCreate(*dock_bar, &frame,
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
                    dock_styles[index],
                dock_ids[index])) {
            DestroyDockBar(*dock_bar);
            delete dock_bar;
            ThrowMfcResourceException();
        }
        frame.control_bars.push_back(dock_bar);
    }
}

void FrameWndDockControlBar(MfcFrameWndCompat& frame, MfcControlBarCompat& bar,
    UINT dock_id, const RECT* rect) {
    MfcDockBarCompat* dock_bar = nullptr;
    if (dock_id != 0) {
        dock_bar = static_cast<MfcDockBarCompat*>(
            FrameWndGetControlBar(frame, dock_id));
    }
    FrameWndDockControlBarToDockBar(frame, bar, dock_bar, rect);
}

void FrameWndDockControlBarToDockBar(MfcFrameWndCompat& frame,
    MfcControlBarCompat& bar, MfcDockBarCompat* dock_bar, const RECT* rect) {
    if (bar.dock_context == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0x59, nullptr,
            "control bar must enable docking before DockControlBar");
    }
    if (dock_bar == nullptr) {
        constexpr UINT dock_ids[] = {0xe81b, 0xe81c, 0xe81d, 0xe81e};
        constexpr DWORD dock_styles[] = {
            kControlBarAlignTop, kControlBarAlignLeft,
            kControlBarAlignRight, kControlBarAlignBottom};
        const DWORD align = bar.bar_style & kControlBarAlignAny;
        for (std::size_t index = 0; index < std::size(dock_ids); ++index) {
            if ((dock_styles[index] & kControlBarAlignAny) == align) {
                dock_bar = static_cast<MfcDockBarCompat*>(
                    FrameWndGetControlBar(frame, dock_ids[index]));
                if (dock_bar == nullptr) {
                    CrtDbgReport(2, "winfrm2.cpp", 0x63, nullptr,
                        "matching frame dock bar is missing");
                }
                break;
            }
        }
    }
    if (dock_bar == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0x6a, nullptr,
            "target dock bar is required");
        ThrowMfcResourceException();
    }
    if (std::find(frame.control_bars.begin(), frame.control_bars.end(), &bar) ==
        frame.control_bars.end()) {
        CrtDbgReport(2, "winfrm2.cpp", 0x6b, nullptr,
            "control bar is not in the frame control-bar list");
        frame.control_bars.push_back(&bar);
    }
    if (bar.owner_frame != &frame) {
        CrtDbgReport(2, "winfrm2.cpp", 0x6c, nullptr,
            "control bar owner frame mismatch");
        bar.owner_frame = &frame;
    }
    DockBarDockControlBar(*dock_bar, bar, rect);
}

void FrameWndDockControlBarAtRect(MfcFrameWndCompat& frame,
    MfcControlBarCompat& bar, MfcDockBarCompat* dock_bar, const RECT* rect) {
    if (bar.dock_context == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0x78, nullptr,
            "control bar must enable docking before DockControlBar");
    }
    if (dock_bar == nullptr) {
        constexpr UINT dock_ids[] = {0xe81b, 0xe81c, 0xe81d, 0xe81e};
        constexpr DWORD dock_styles[] = {
            kControlBarAlignTop, kControlBarAlignLeft,
            kControlBarAlignRight, kControlBarAlignBottom};
        MfcDockBarCompat* style_match = nullptr;
        const UINT id = control_bar_window_id(bar);
        const DWORD align = bar.bar_style & kControlBarAlignAny;
        for (std::size_t index = 0; index < std::size(dock_ids); ++index) {
            auto* candidate = static_cast<MfcDockBarCompat*>(
                FrameWndGetControlBar(frame, dock_ids[index]));
            if (candidate != nullptr &&
                DockBarFindBar(*candidate, id, -1) > 0) {
                dock_bar = candidate;
                break;
            }
            if ((dock_styles[index] & kControlBarAlignAny) == align) {
                style_match = candidate;
                if (style_match == nullptr) {
                    CrtDbgReport(2, "winfrm2.cpp", 0x92, nullptr,
                        "matching frame dock bar is missing");
                }
            }
        }
        if (dock_bar == nullptr) {
            dock_bar = style_match;
        }
    }
    if (dock_bar == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0x9c, nullptr,
            "target dock bar is required");
        ThrowMfcResourceException();
    }
    if (std::find(frame.control_bars.begin(), frame.control_bars.end(), &bar) ==
        frame.control_bars.end()) {
        CrtDbgReport(2, "winfrm2.cpp", 0x9d, nullptr,
            "control bar is not in the frame control-bar list");
        frame.control_bars.push_back(&bar);
    }
    if (bar.owner_frame != &frame) {
        CrtDbgReport(2, "winfrm2.cpp", 0x9e, nullptr,
            "control bar owner frame mismatch");
        bar.owner_frame = &frame;
    }
    DockBarDockControlBarAtRect(*dock_bar, bar, rect);
}

void FrameWndFloatControlBar(MfcFrameWndCompat& frame, MfcControlBarCompat& bar,
    POINT point, DWORD dock_style) {
    if (bar.owner_frame != nullptr && bar.dock_bar != nullptr) {
        MfcDockBarCompat* current_dock_bar = bar.dock_bar;
        if (!ObjectIsKindOfRuntimeClass(current_dock_bar, GetDockBarRuntimeClass())) {
            CrtDbgReport(2, "winfrm2.cpp", 0xb0, nullptr,
                "control bar must be docked in a CDockBar");
        }
        if (current_dock_bar->floating &&
            DockBarGetDockedCount(*current_dock_bar) == 1 &&
            (dock_style & current_dock_bar->bar_style & kControlBarAlignAny) != 0) {
            MfcCWndCompat* parent = CWndGetParentInline(*current_dock_bar);
            if (parent == nullptr) {
                CrtDbgReport(2, "winfrm2.cpp", 0xb6, nullptr,
                    "floating dock bar must have a parent frame");
            } else if (!ObjectIsKindOfRuntimeClass(parent,
                           GetMiniDockFrameRuntimeClass())) {
                CrtDbgReport(2, "winfrm2.cpp", 0xb7, nullptr,
                    "floating dock bar parent must be CMiniDockFrameWnd");
            } else {
                CWndSetWindowPos(*parent, nullptr, point.x, point.y, 0, 0,
                    SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                FrameWndRecalcLayout(*static_cast<MfcFrameWndCompat*>(parent),
                    true);
                CWndUpdateWindowInline(*parent);
                return;
            }
        }
    }
    DWORD floating_style = dock_style;
    if ((bar.bar_style & 0x04U) != 0) {
        floating_style |= 0x04U;
        if ((dock_style & (kControlBarAlignTop | kControlBarAlignLeft)) != 0) {
            floating_style = (dock_style & ~kControlBarAlignAny) |
                kControlBarAlignBottom | 0x04U;
        }
    }
    MfcMiniDockFrameWndCompat* mini =
        FrameWndCreateFloatingFrame(frame, floating_style);
    if (mini == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0xcb, nullptr,
            "floating frame creation failed");
        ThrowMfcResourceException();
    }
    if (mini->window != nullptr) {
        CWndSetWindowPos(*mini, nullptr, point.x, point.y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (mini->window != nullptr && bar.window != nullptr &&
        GetWindow(mini->window, GW_OWNER) == nullptr) {
        SetWindowLongPtrA(mini->window, GWLP_HWNDPARENT,
            reinterpret_cast<LONG_PTR>(bar.window));
    }
    MfcDockBarCompat* mini_dock_bar = &mini->dock_bar;
    MfcCWndCompat* dock_item = CWndGetDlgItem(*mini, 0xe81f);
    if (dock_item == nullptr) {
        CrtDbgReport(2, "winfrm2.cpp", 0xd2, nullptr,
            "mini dock frame dock bar is missing");
    } else if (!ObjectIsKindOfRuntimeClass(dock_item, GetDockBarRuntimeClass())) {
        CrtDbgReport(2, "winfrm2.cpp", 0xd3, nullptr,
            "mini dock frame child must be a CDockBar");
    } else {
        mini_dock_bar = static_cast<MfcDockBarCompat*>(dock_item);
    }
    if (bar.owner_frame != &frame) {
        CrtDbgReport(2, "winfrm2.cpp", 0xd5, nullptr,
            "control bar owner frame mismatch");
        bar.owner_frame = &frame;
    }
    DockBarDockControlBar(*mini_dock_bar, bar, nullptr);
    FrameWndRecalcLayout(*mini, true);
    if (mini->window != nullptr && IsWindow(mini->window)) {
        const DWORD style = static_cast<DWORD>(
            GetWindowLongA(bar.window, GWL_STYLE));
        if ((style & WS_VISIBLE) != 0) {
            CWndShowWindow(*mini, SW_SHOWNA);
            CWndUpdateWindowInline(*mini);
        }
    }
}

DWORD FrameWndCanDock(MfcFrameWndCompat& frame, POINT point, DWORD dock_style,
    MfcDockBarCompat** dock_bar) {
    if (dock_bar != nullptr) {
        *dock_bar = nullptr;
    }
    DWORD allowed = dock_style & (kControlBarAlignAny | 0x40U);
    if (allowed == 0) {
        return 0;
    }
    for (void* entry : frame.control_bars) {
        auto* candidate = static_cast<MfcDockBarCompat*>(entry);
        if (candidate == nullptr ||
            !ObjectIsKindOfRuntimeClass(candidate, GetDockBarRuntimeClass()) ||
            candidate->window == nullptr || !IsWindow(candidate->window) ||
            !ControlBarIsVisible(*candidate) ||
            !CWndIsWindowVisibleInline(*candidate)) {
            continue;
        }
        const DWORD style = candidate->bar_style & (kControlBarAlignAny | 0x40U);
        if ((style & allowed & kControlBarAlignAny) == 0 ||
            (candidate->floating &&
                (style & allowed & 0x40U) == 0)) {
            continue;
        }
        RECT rect{};
        GetWindowRect(candidate->window, &rect);
        if (rect.right == rect.left) {
            ++rect.right;
        }
        if (rect.bottom == rect.top) {
            ++rect.bottom;
        }
        RECT point_rect{point.x, point.y, point.x + 1, point.y + 1};
        RECT hit{};
        if (IntersectRect(&hit, &rect, &point_rect) != 0) {
            if (dock_bar != nullptr) {
                *dock_bar = candidate;
            }
            return style & allowed;
        }
    }
    return 0;
}

namespace {

MfcControlBarCompat* mini_dock_frame_first_bar(
    MfcMiniDockFrameWndCompat& frame) {
    for (std::size_t index = 1; index < frame.dock_bar.bars.size(); ++index) {
        if (frame.dock_bar.bars[index] != nullptr) {
            return frame.dock_bar.bars[index];
        }
    }
    return nullptr;
}

void mini_dock_frame_default_nc_mouse(MfcMiniDockFrameWndCompat& frame,
    UINT message, UINT hit_test, POINT point) {
    if (frame.window != nullptr && IsWindow(frame.window)) {
        DefWindowProcA(frame.window, message, hit_test,
            MAKELPARAM(point.x, point.y));
    }
}

} // namespace

MfcRuntimeClassCompat* GetMiniDockFrameRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CMiniDockFrameWnd",
        static_cast<int>(sizeof(MfcMiniDockFrameWndCompat)), 0xffff,
        +[]() -> void* {
            auto* frame = new MfcMiniDockFrameWndCompat();
            ConstructMiniDockFrameWnd(*frame);
            return frame;
        },
        GetMiniFrameRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetMiniDockFrameMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_SETTEXT, 0, 0, 0, nullptr, 0, nullptr},
        {kMfcIdleUpdateCmdUiMessage, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NCLBUTTONDOWN, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NCLBUTTONDBLCLK, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

MfcMiniDockFrameWndCompat& ConstructMiniDockFrameWnd(
    MfcMiniDockFrameWndCompat& frame) {
    ConstructMiniFrameWnd(frame);
    frame.runtime_class = GetMiniDockFrameRuntimeClass();
    ConstructDockBar(frame.dock_bar, true);
    frame.dock_bar.owner_frame = &frame;
    frame.creating = false;
    frame.dock_style = 0;
    return frame;
}

void DestroyMiniDockFrameWnd(MfcMiniDockFrameWndCompat& frame) {
    DestroyDockBar(frame.dock_bar);
    frame.creating = false;
    frame.dock_style = 0;
    DestroyMiniFrameWnd(frame);
}

MfcMiniDockFrameWndCompat* DeleteMiniDockFrameScalarDtor(
    MfcMiniDockFrameWndCompat* frame, unsigned flags) {
    if (frame == nullptr) {
        return nullptr;
    }
    DestroyMiniDockFrameWnd(*frame);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(frame);
    }
    return frame;
}

bool MiniDockFrameCreate(MfcMiniDockFrameWndCompat& frame,
    MfcCWndCompat* parent, DWORD dock_style) {
    frame.creating = true;
    frame.dock_style = dock_style;
    AfxDeferRegisterClass(8);
    AfxDeferRegisterClass(2);

    const DWORD frame_style = (dock_style & 0x04U) != 0
        ? 0x80c83300UL : 0x80c83b00UL;
    RECT empty{};
    HWND parent_hwnd = parent == nullptr ? nullptr : parent->window;
    if (!CreateWindowExFromRect(frame, 0, "AfxFrameOrView42sd", "",
            frame_style, empty, parent_hwnd, nullptr, nullptr)) {
        frame.creating = false;
        return false;
    }

    const DWORD align_style = (dock_style & 0x5000U) != 0
        ? kControlBarAlignTop : kControlBarAlignBottom;
    const DWORD dock_bar_style = WS_CHILD | WS_VISIBLE |
        (dock_style & 0x40U) | align_style;
    if (!DockBarCreate(frame.dock_bar, &frame, dock_bar_style, 0xe81f)) {
        frame.creating = false;
        return false;
    }
    frame.dock_bar.owner_frame = &frame;
    frame.creating = false;
    return true;
}

void MiniDockFrameOnSetText(MfcMiniDockFrameWndCompat& frame,
    const char* text) {
    if (frame.creating) {
        return;
    }
    if (frame.window != nullptr && IsWindow(frame.window)) {
        SetWindowTextIfChanged(frame.window, text == nullptr ? "" : text);
    }
    if (frame.dock_bar.window != nullptr && IsWindow(frame.dock_bar.window)) {
        SetWindowTextIfChanged(frame.dock_bar.window,
            text == nullptr ? "" : text);
    }
}

void MiniDockFrameOnUpdateCmdUI(MfcMiniDockFrameWndCompat& frame,
    bool disable_if_no_handler) {
    DockBarOnUpdateCmdUI(frame.dock_bar, nullptr, disable_if_no_handler);
}

int MiniDockFrameOnMouseActivate(MfcMiniDockFrameWndCompat& frame,
    MfcCWndCompat* top_level, UINT hit_test, UINT message) {
    (void)frame;
    (void)top_level;
    (void)message;
    if (hit_test >= HTLEFT && hit_test <= HTBOTTOMRIGHT) {
        return MA_NOACTIVATE;
    }
    return MA_ACTIVATE;
}

void MiniDockFrameOnNcLButtonDown(MfcMiniDockFrameWndCompat& frame,
    UINT hit_test, POINT point) {
    if (hit_test == HTCAPTION && (frame.dock_bar.bar_style & 0x40U) == 0) {
        if (MfcControlBarCompat* bar = mini_dock_frame_first_bar(frame)) {
            if (bar->dock_context != nullptr) {
                DockContextStartDrag(*bar->dock_context, point);
                return;
            }
        }
    } else if (hit_test >= HTLEFT && hit_test <= HTBOTTOMRIGHT) {
        if ((frame.dock_bar.bar_style & 0x40U) != 0) {
            CrtDbgReport(2, "bardock.cpp", 0x367, nullptr,
                "resizing is not valid for fixed floating dock bars");
            return;
        }
        if (MfcControlBarCompat* bar = mini_dock_frame_first_bar(frame)) {
            if (bar->dock_context != nullptr) {
                DockContextStartResize(*bar->dock_context,
                    static_cast<int>(hit_test), point);
                return;
            }
        }
    }
    mini_dock_frame_default_nc_mouse(frame, WM_NCLBUTTONDOWN, hit_test, point);
}

void MiniDockFrameOnNcLButtonDblClk(MfcMiniDockFrameWndCompat& frame,
    UINT hit_test, POINT point) {
    if (hit_test == HTCAPTION && (frame.dock_bar.bar_style & 0x40U) == 0) {
        if (MfcControlBarCompat* bar = mini_dock_frame_first_bar(frame)) {
            if (bar->dock_context != nullptr) {
                DockContextToggleDocking(*bar->dock_context);
                return;
            }
        }
    }
    mini_dock_frame_default_nc_mouse(frame, WM_NCLBUTTONDBLCLK, hit_test,
        point);
}

HBITMAP LoadSysColorBitmap(HMODULE module, HRSRC resource, bool monochrome) {
    (void)monochrome;
    if (module == nullptr) {
        module = GetModuleHandleA(nullptr);
    }
    if (resource == nullptr) {
        return nullptr;
    }
    HGLOBAL data = LoadResource(module, resource);
    if (data == nullptr) {
        return nullptr;
    }
    const void* bits = LockResource(data);
    if (bits == nullptr) {
        return nullptr;
    }
    const auto* info = static_cast<const BITMAPINFO*>(bits);
    HDC dc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBitmap(dc, &info->bmiHeader, CBM_INIT,
        reinterpret_cast<const BYTE*>(bits) + info->bmiHeader.biSize +
            (info->bmiHeader.biClrUsed != 0 ? info->bmiHeader.biClrUsed :
                (info->bmiHeader.biBitCount <= 8
                    ? (1U << info->bmiHeader.biBitCount) : 0U)) *
                sizeof(RGBQUAD),
        info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return bitmap;
}

namespace {

bool toolbar_valid_window(const MfcToolBarCompat& toolbar) {
    return toolbar.window != nullptr && IsWindow(toolbar.window) != 0;
}

LRESULT toolbar_send(const MfcToolBarCompat& toolbar, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return toolbar_valid_window(toolbar)
        ? SendMessageA(toolbar.window, message, wparam, lparam) : 0;
}

TBBUTTON toolbar_external_button(TBBUTTON button) {
    button.fsState ^= TBSTATE_ENABLED;
    return button;
}

TBBUTTON toolbar_internal_button(TBBUTTON button) {
    button.fsState ^= TBSTATE_ENABLED;
    return button;
}

int toolbar_button_extent(const MfcToolBarCompat& toolbar,
    const TBBUTTON& button) {
    if ((button.fsStyle & BTNS_SEP) != 0) {
        return std::max<int>(1, button.iBitmap);
    }
    return std::max<LONG>(1, toolbar.button_size.cx);
}

} // namespace

bool ToolBarGetButton(MfcToolBarCompat& toolbar, int index,
    TBBUTTON& button) {
    if (!toolbar_valid_window(toolbar)) {
        std::memset(&button, 0, sizeof(button));
        return false;
    }
    if (toolbar_send(toolbar, TB_GETBUTTON, static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(&button)) == 0) {
        std::memset(&button, 0, sizeof(button));
        return false;
    }
    button = toolbar_external_button(button);
    return true;
}

bool ToolBarSetButton(MfcToolBarCompat& toolbar, int index,
    const TBBUTTON& button) {
    if (!toolbar_valid_window(toolbar)) {
        return false;
    }
    TBBUTTON internal = toolbar_internal_button(button);
    TBBUTTON current{};
    if (toolbar_send(toolbar, TB_GETBUTTON, static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(&current)) != 0 &&
        std::memcmp(&current, &internal, sizeof(TBBUTTON)) == 0) {
        return true;
    }
    const DWORD visible = static_cast<DWORD>(CWndGetStyle(toolbar)) &
        WS_VISIBLE;
    CWndModifyStyle(toolbar, WS_VISIBLE, 0, 0);
    toolbar_send(toolbar, TB_DELETEBUTTON, static_cast<WPARAM>(index), 0);
    const LRESULT inserted = toolbar_send(toolbar, TB_INSERTBUTTONA,
        static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&internal));
    CWndModifyStyle(toolbar, 0, visible, 0);
    toolbar.button_layout_dirty = true;
    if (inserted != 0) {
        RECT rect{};
        if (toolbar_send(toolbar, TB_GETITEMRECT, static_cast<WPARAM>(index),
            reinterpret_cast<LPARAM>(&rect)) != 0) {
            InvalidateRect(toolbar.window, &rect, TRUE);
        } else {
            InvalidateRect(toolbar.window, nullptr, TRUE);
        }
    }
    return inserted != 0;
}

int ToolBarCommandToIndex(MfcToolBarCompat& toolbar, int command_id) {
    return static_cast<int>(toolbar_send(toolbar, TB_COMMANDTOINDEX,
        static_cast<WPARAM>(command_id), 0));
}

int ToolBarGetItemID(MfcToolBarCompat& toolbar, int index) {
    TBBUTTON button{};
    return ToolBarGetButton(toolbar, index, button) ? button.idCommand : 0;
}

bool ToolBarGetItemRect(MfcToolBarCompat& toolbar, int index, RECT& rect) {
    if (!toolbar_valid_window(toolbar)) {
        SetRectEmpty(&rect);
        return false;
    }
    if (toolbar.button_layout_dirty) {
        ToolBarInvalidateButtonLayout(toolbar);
    }
    if (toolbar_send(toolbar, TB_GETITEMRECT, static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(&rect)) == 0) {
        SetRectEmpty(&rect);
        return false;
    }
    return true;
}

void ToolBarInvalidateButtonLayout(MfcToolBarCompat& toolbar) {
    toolbar.button_layout_dirty = false;
    if (toolbar_valid_window(toolbar)) {
        toolbar_send(toolbar, TB_AUTOSIZE, 0, 0);
        InvalidateRect(toolbar.window, nullptr, TRUE);
    }
}

UINT ToolBarGetButtonStyle(MfcToolBarCompat& toolbar, int index) {
    TBBUTTON button{};
    if (!ToolBarGetButton(toolbar, index, button)) {
        return 0;
    }
    return static_cast<UINT>(button.fsStyle) |
        (static_cast<UINT>(button.fsState) << 16);
}

void ToolBarSetButtonStyle(MfcToolBarCompat& toolbar, int index, UINT style) {
    TBBUTTON button{};
    if (!ToolBarGetButton(toolbar, index, button)) {
        return;
    }
    const BYTE new_style = static_cast<BYTE>(style & 0xffU);
    const BYTE new_state = static_cast<BYTE>((style >> 16) & 0xffU);
    if (button.fsStyle == new_style && button.fsState == new_state) {
        return;
    }
    button.fsStyle = new_style;
    button.fsState = new_state;
    ToolBarSetButton(toolbar, index, button);
}

SIZE ToolBarCalcSize(MfcToolBarCompat& toolbar, const TBBUTTON* buttons,
    int count) {
    SIZE size{};
    if (buttons == nullptr || count <= 0) {
        return size;
    }
    int row_width = 0;
    int row_height = std::max<LONG>(1, toolbar.button_size.cy);
    for (int index = 0; index < count; ++index) {
        const TBBUTTON& button = buttons[index];
        if ((button.fsState & TBSTATE_HIDDEN) != 0) {
            continue;
        }
        const int width = toolbar_button_extent(toolbar, button);
        row_width += width;
        size.cx = std::max<LONG>(size.cx, row_width);
        size.cy = std::max<LONG>(size.cy, row_height);
        if ((button.fsState & TBSTATE_WRAP) != 0) {
            row_width = 0;
            size.cy += row_height;
        }
    }
    return size;
}

int ToolBarWrapToolBar(MfcToolBarCompat& toolbar, TBBUTTON* buttons,
    int count, int width) {
    if (buttons == nullptr || count <= 0) {
        return 0;
    }
    int rows = 1;
    int row_width = 0;
    const int limit = width <= 0 ? 0x7fff : width;
    for (int index = 0; index < count; ++index) {
        buttons[index].fsState &= ~TBSTATE_WRAP;
        if ((buttons[index].fsState & TBSTATE_HIDDEN) != 0) {
            continue;
        }
        const int extent = toolbar_button_extent(toolbar, buttons[index]);
        if (row_width > 0 && row_width + extent > limit) {
            buttons[index - 1].fsState |= TBSTATE_WRAP;
            row_width = 0;
            ++rows;
        }
        row_width += extent;
    }
    return rows;
}

void ToolBarSizeToolBar(MfcToolBarCompat& toolbar, TBBUTTON* buttons,
    int count, int length, bool vertical) {
    if (buttons == nullptr || count <= 0) {
        return;
    }
    if (vertical) {
        for (int index = 0; index < count - 1; ++index) {
            buttons[index].fsState |= TBSTATE_WRAP;
        }
        return;
    }
    ToolBarWrapToolBar(toolbar, buttons, count,
        length <= 0 ? toolbar.mru_width : length);
}

SIZE ToolBarCalcLayout(MfcToolBarCompat& toolbar, DWORD mode, int length) {
    const int count = static_cast<int>(toolbar_send(toolbar, TB_BUTTONCOUNT,
        0, 0));
    std::vector<TBBUTTON> buttons(static_cast<std::size_t>(std::max(0, count)));
    for (int index = 0; index < count; ++index) {
        ToolBarGetButton(toolbar, index,
            buttons[static_cast<std::size_t>(index)]);
    }
    if (!buttons.empty()) {
        ToolBarSizeToolBar(toolbar, buttons.data(), count, length,
            (mode & 0x02U) != 0);
    }
    SIZE size = ToolBarCalcSize(toolbar, buttons.data(), count);
    if ((mode & 0x40U) != 0) {
        for (int index = 0; index < count; ++index) {
            ToolBarSetButton(toolbar, index,
                buttons[static_cast<std::size_t>(index)]);
        }
        if ((toolbar.bar_style & 0x05U) == 0x05U) {
            toolbar.mru_width = size.cx;
        }
    }
    RECT inside{0, 0, size.cx, size.cy};
    ControlBarCalcInsideRect(toolbar, inside,
        (mode & 0x02U) != 0);
    SIZE fixed = ControlBarCalcFixedLayout(toolbar, (mode & 0x01U) != 0,
        (mode & 0x02U) != 0);
    size.cx = std::max<LONG>(size.cx - rect_width(inside), fixed.cx);
    size.cy = std::max<LONG>(size.cy - rect_height(inside), fixed.cy);
    if (size.cx == 0) {
        size.cx = rect_width(inside);
    }
    if (size.cy == 0) {
        size.cy = rect_height(inside);
    }
    return size;
}

SIZE ToolBarCalcFixedLayout(MfcToolBarCompat& toolbar, bool stretch,
    bool horizontal) {
    DWORD mode = horizontal ? 0x01U : 0x00U;
    if (stretch) {
        mode |= 0x02U;
    }
    return ToolBarCalcLayout(toolbar, mode, -1);
}

SIZE ToolBarCalcDynamicLayout(MfcToolBarCompat& toolbar, int length,
    DWORD mode) {
    if (length == -1 && (mode & (0x04U | 0x40U)) == 0 &&
        ((mode & 0x08U) != 0 || (mode & 0x10U) != 0)) {
        return ControlBarCalcFixedLayout(toolbar, (mode & 0x01U) != 0,
            (mode & 0x08U) != 0);
    }
    return ToolBarCalcLayout(toolbar, mode, length);
}

void ToolBarGetButtonInfo(MfcToolBarCompat& toolbar, int index,
    int& image, UINT& style, int& command_id) {
    TBBUTTON button{};
    if (!ToolBarGetButton(toolbar, index, button)) {
        image = 0;
        style = 0;
        command_id = 0;
        return;
    }
    image = button.iBitmap;
    style = static_cast<UINT>(button.fsStyle) |
        (static_cast<UINT>(button.fsState) << 16);
    command_id = button.idCommand;
}

void ToolBarSetButtonInfo(MfcToolBarCompat& toolbar, int index,
    int image, UINT style, int command_id) {
    TBBUTTON button{};
    if (!ToolBarGetButton(toolbar, index, button)) {
        return;
    }
    TBBUTTON old = button;
    button.iBitmap = image;
    button.idCommand = command_id;
    button.fsStyle = static_cast<BYTE>(style & 0xffU);
    button.fsState = static_cast<BYTE>((style >> 16) & 0xffU);
    if (std::memcmp(&old, &button, sizeof(TBBUTTON)) != 0) {
        ToolBarSetButton(toolbar, index, button);
        toolbar.button_layout_dirty = true;
    }
}

UINT ToolBarOnToolHitTest(MfcToolBarCompat& toolbar, POINT point,
    TOOLINFOA* tool_info) {
    UINT hit = CWndOnToolHitTest(toolbar, point, tool_info);
    if (hit != static_cast<UINT>(-1)) {
        return hit;
    }
    const int count = static_cast<int>(toolbar_send(toolbar, TB_BUTTONCOUNT,
        0, 0));
    for (int index = 0; index < count; ++index) {
        RECT rect{};
        if (!ToolBarGetItemRect(toolbar, index, rect) ||
            !PtInRect(&rect, point)) {
            continue;
        }
        TBBUTTON button{};
        if (!ToolBarGetButton(toolbar, index, button) ||
            (button.fsState & TBSTATE_HIDDEN) != 0 ||
            (button.fsStyle & BTNS_SEP) != 0) {
            return static_cast<UINT>(-1);
        }
        if (tool_info != nullptr && tool_info->cbSize >= sizeof(TOOLINFOA)) {
            tool_info->hwnd = toolbar.window;
            tool_info->uId = static_cast<UINT_PTR>(button.idCommand);
            tool_info->rect = rect;
            tool_info->lParam = static_cast<LPARAM>(index);
        }
        return static_cast<UINT>(button.idCommand);
    }
    return static_cast<UINT>(-1);
}

bool ToolBarSetButtonText(MfcToolBarCompat& toolbar, int index,
    const char* text) {
    if (text == nullptr) {
        text = "";
    }
    int string_index = -1;
    if (toolbar_valid_window(toolbar)) {
        string_index = static_cast<int>(toolbar_send(toolbar, TB_ADDSTRINGA,
            0, reinterpret_cast<LPARAM>(text)));
    }
    if (string_index < 0) {
        string_index = static_cast<int>(toolbar.button_text.size());
    }
    toolbar.button_text[index] = text;
    TBBUTTON button{};
    if (ToolBarGetButton(toolbar, index, button)) {
        button.iString = string_index;
        ToolBarSetButton(toolbar, index, button);
    }
    return true;
}

std::string ToolBarFormatButtonText(MfcToolBarCompat& toolbar, int index) {
    return ToolBarGetButtonText(toolbar, index);
}

std::string ToolBarGetButtonText(MfcToolBarCompat& toolbar, int index) {
    auto found = toolbar.button_text.find(index);
    if (found != toolbar.button_text.end()) {
        return found->second;
    }
    TBBUTTON button{};
    if (!ToolBarGetButton(toolbar, index, button) || button.idCommand == 0 ||
        !toolbar_valid_window(toolbar)) {
        return {};
    }
    LRESULT length = toolbar_send(toolbar, TB_GETBUTTONTEXTA,
        static_cast<WPARAM>(button.idCommand), 0);
    if (length <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(length) + 1U, '\0');
    toolbar_send(toolbar, TB_GETBUTTONTEXTA,
        static_cast<WPARAM>(button.idCommand),
        reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<std::size_t>(length));
    toolbar.button_text[index] = text;
    return text;
}

MfcRuntimeClassCompat* GetToolBarRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CToolBar", static_cast<int>(sizeof(MfcToolBarCompat)), 0xffff,
        +[]() -> void* {
            auto* toolbar = new MfcToolBarCompat();
            ConstructControlBar(*toolbar);
            toolbar->runtime_class = GetToolBarRuntimeClass();
            return toolbar;
        },
        GetControlBarRuntimeClass(), nullptr};
    return &runtime_class;
}

void ToolBarOnNcDestroy(MfcToolBarCompat& toolbar) {
    if (toolbar.window != nullptr) {
        DefWindowProcA(toolbar.window, WM_NCDESTROY, 0, 0);
    }
}

bool ToolBarDefaultTrue() {
    return true;
}

void ToolBarCalcInsideRect(MfcToolBarCompat& toolbar, RECT& rect,
    bool horizontal) {
    ControlBarCalcInsideRect(toolbar, rect, horizontal);
    if (!horizontal && rect_width(rect) > 2) {
        rect.left += 2;
    }
    if (horizontal && rect_height(rect) > 2) {
        rect.top += 2;
    }
    clamp_rect(rect);
}

void ToolBarOnBarStyleChange(MfcToolBarCompat& toolbar, DWORD old_style,
    DWORD new_style) {
    if (((old_style ^ new_style) & 0xf00U) != 0 && toolbar.window != nullptr) {
        CWndSetWindowPos(toolbar, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_FRAMECHANGED);
    }
    toolbar.button_layout_dirty = true;
}

void ToolBarEraseNonClient(MfcToolBarCompat& toolbar) {
    ControlBarEraseNonClient(toolbar);
}

void ToolBarOnWindowPosChanging(MfcToolBarCompat& toolbar,
    WINDOWPOS& window_pos) {
    const DWORD old_style = toolbar.bar_style;
    toolbar.bar_style &= ~0xf00U;
    ControlBarOnWindowPosChanging(toolbar, window_pos);
    toolbar.bar_style = old_style;
    if ((old_style & 0x04U) != 0 && (window_pos.flags & SWP_NOSIZE) == 0 &&
        toolbar.window != nullptr) {
        InvalidateRect(toolbar.window, nullptr, TRUE);
    }
}

void ToolBarOnNcCalcSize(MfcToolBarCompat& toolbar) {
    if (toolbar.button_layout_dirty) {
        ToolBarInvalidateButtonLayout(toolbar);
    }
    if (toolbar.window != nullptr) {
        DefWindowProcA(toolbar.window, WM_NCCALCSIZE, 0, 0);
    }
}

void ToolBarOnSetButtonSize(MfcToolBarCompat& toolbar, LPARAM size) {
    ToolBarHandleSizeMessage(toolbar, toolbar.button_size, TB_SETBUTTONSIZE,
        size);
}

void ToolBarOnSetBitmapSize(MfcToolBarCompat& toolbar, LPARAM size) {
    ToolBarHandleSizeMessage(toolbar, toolbar.image_size, TB_SETBITMAPSIZE,
        size);
}

LRESULT ToolBarHandleSizeMessage(MfcToolBarCompat& toolbar, SIZE& target,
    UINT message, LPARAM lparam) {
    const DWORD old_style = static_cast<DWORD>(CWndGetStyle(toolbar));
    CWndModifyStyle(toolbar, 0, TBSTYLE_FLAT | TBSTYLE_TRANSPARENT, 0);
    LRESULT result = toolbar.window == nullptr ? 0 :
        DefWindowProcA(toolbar.window, message, 0, lparam);
    if (result != 0) {
        target.cx = LOWORD(lparam);
        target.cy = HIWORD(lparam);
    }
    if (toolbar.window != nullptr) {
        SetWindowLongA(toolbar.window, GWL_STYLE, old_style);
    }
    return result;
}

LRESULT ToolBarDefaultWithStylePatch(MfcToolBarCompat& toolbar, UINT message,
    WPARAM wparam, LPARAM lparam) {
    const DWORD old_style = static_cast<DWORD>(CWndGetStyle(toolbar));
    CWndModifyStyle(toolbar, 0, TBSTYLE_FLAT | TBSTYLE_TRANSPARENT, 0);
    LRESULT result = toolbar.window == nullptr ? 0 :
        DefWindowProcA(toolbar.window, message, wparam, lparam);
    if (toolbar.window != nullptr) {
        SetWindowLongA(toolbar.window, GWL_STYLE, old_style);
    }
    return result;
}

void ToolBarReloadBitmap(MfcToolBarCompat& toolbar) {
    if (toolbar.image_instance == nullptr || toolbar.image_resource == nullptr) {
        return;
    }
    HBITMAP bitmap = LoadSysColorBitmap(toolbar.image_instance,
        toolbar.image_resource, false);
    if (bitmap != nullptr) {
        if (toolbar.image_well != nullptr) {
            DeleteObject(toolbar.image_well);
        }
        toolbar.image_well = bitmap;
    }
}

void ToolBarCmdUIEnable(MfcToolBarCompat& toolbar, MfcCmdUICompat& cmd_ui,
    bool enabled) {
    cmd_ui.changed = true;
    UINT style = ToolBarGetButtonStyle(toolbar, static_cast<int>(cmd_ui.index));
    style &= ~0x00040000U;
    if (!enabled) {
        style |= 0x00040000U;
    }
    ToolBarSetButtonStyle(toolbar, static_cast<int>(cmd_ui.index), style);
}

void ToolBarCmdUISetCheck(MfcToolBarCompat& toolbar, MfcCmdUICompat& cmd_ui,
    int check) {
    if (check < 0) {
        check = 0;
    }
    if (check > 2) {
        check = 2;
    }
    cmd_ui.changed = true;
    UINT style = ToolBarGetButtonStyle(toolbar, static_cast<int>(cmd_ui.index));
    style &= ~0x00110000U;
    if (check == 1) {
        style |= 0x00010000U;
    } else if (check == 2) {
        style |= 0x00100000U;
    }
    ToolBarSetButtonStyle(toolbar, static_cast<int>(cmd_ui.index),
        style | 0x02U);
}

void ToolBarCmdUISetText(MfcToolBarCompat& toolbar, MfcCmdUICompat& cmd_ui,
    const char* text) {
    ToolBarSetButtonText(toolbar, static_cast<int>(cmd_ui.index), text);
    cmd_ui.changed = true;
}

void ToolBarOnUpdateCmdUI(MfcToolBarCompat& toolbar,
    MfcCommandTargetCompat* target, bool disable_if_no_handler) {
    const int count = static_cast<int>(toolbar_send(toolbar, TB_BUTTONCOUNT,
        0, 0));
    for (int index = 0; index < count; ++index) {
        TBBUTTON button{};
        if (!ToolBarGetButton(toolbar, index, button) ||
            (button.fsStyle & BTNS_SEP) != 0) {
            continue;
        }
        MfcCmdUICompat cmd_ui{};
        ConstructCmdUI(cmd_ui);
        cmd_ui.kind = MfcCmdUIKind::ToolBar;
        cmd_ui.index = static_cast<UINT>(index);
        cmd_ui.id = static_cast<UINT>(button.idCommand);
        cmd_ui.other = &toolbar;
        if (target != nullptr) {
            CmdUIDoUpdate(cmd_ui, *target, disable_if_no_handler);
        } else if (disable_if_no_handler) {
            ToolBarCmdUIEnable(toolbar, cmd_ui, false);
        }
    }
    if (MfcCWndCompat* owner = control_bar_owner(toolbar)) {
        CWndUpdateDialogControls(toolbar, owner, disable_if_no_handler);
    }
}

void ToolBarAssertValid(MfcToolBarCompat& toolbar) {
    ControlBarAssertValid(toolbar);
    if (toolbar.image_well != nullptr &&
        GetObjectType(toolbar.image_well) != OBJ_BITMAP) {
        AfxTraceOutput("Warning: CToolBar image well is not a bitmap.\n");
    }
    if (toolbar.image_instance != nullptr && toolbar.image_well != nullptr &&
        toolbar.image_resource == nullptr) {
        AfxTraceOutput("Warning: CToolBar image resource missing.\n");
    }
}

void ToolBarDump(MfcToolBarCompat& toolbar) {
    CWndDump(toolbar);
    AfxTraceOutput("m_hbmImageWell = %p\n", toolbar.image_well);
    AfxTraceOutput("m_hInstImageWell = %p\n", toolbar.image_instance);
    AfxTraceOutput("m_hRsrcImageWell = %p\n", toolbar.image_resource);
    AfxTraceOutput("m_sizeButton = (%ld,%ld)\n", toolbar.button_size.cx,
        toolbar.button_size.cy);
    AfxTraceOutput("m_sizeImage = (%ld,%ld)\n", toolbar.image_size.cx,
        toolbar.image_size.cy);
    const int count = static_cast<int>(toolbar_send(toolbar, TB_BUTTONCOUNT,
        0, 0));
    for (int index = 0; index < count; ++index) {
        TBBUTTON button{};
        if (ToolBarGetButton(toolbar, index, button)) {
            AfxTraceOutput("toolbar button[%d]: id=%d style=0x%02x state=0x%02x image=%d\n",
                index, button.idCommand, button.fsStyle, button.fsState,
                button.iBitmap);
        }
    }
}

void ToolBarSetOwnerWindow(MfcToolBarCompat& toolbar, MfcCWndCompat* owner) {
    toolbar.owner_frame = owner;
    if (toolbar_valid_window(toolbar)) {
        toolbar_send(toolbar, TB_SETPARENT,
            reinterpret_cast<WPARAM>(owner == nullptr ? nullptr : owner->window),
            0);
    }
}

void ToolBarSetSizes(MfcToolBarCompat& toolbar, SIZE button_size,
    SIZE image_size) {
    if (button_size.cx > 0 && button_size.cy > 0) {
        toolbar.button_size = button_size;
    }
    if (image_size.cx > 0 && image_size.cy > 0) {
        toolbar.image_size = image_size;
    }
    if (toolbar_valid_window(toolbar)) {
        toolbar_send(toolbar, TB_SETBITMAPSIZE, 0,
            MAKELPARAM(toolbar.image_size.cx, toolbar.image_size.cy));
        toolbar_send(toolbar, TB_SETBUTTONSIZE, 0,
            MAKELPARAM(toolbar.button_size.cx, toolbar.button_size.cy));
        InvalidateRect(toolbar.window, nullptr, TRUE);
    }
}

void ToolBarSetHeight(MfcToolBarCompat& toolbar, int height) {
    const int remaining = height - toolbar.button_size.cy;
    toolbar.cy_top_border = std::max(0, remaining / 2);
    toolbar.cy_bottom_border = std::max(0, remaining - toolbar.cy_top_border);
    if (toolbar_valid_window(toolbar)) {
        SetWindowPos(toolbar.window, nullptr, 0, 0, 0, height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(toolbar.window, nullptr, TRUE);
    }
}

void ToolBarSetBitmapHandle(MfcToolBarCompat& toolbar, HBITMAP bitmap) {
    toolbar.image_instance = nullptr;
    toolbar.image_resource = nullptr;
    ToolBarInstallBitmapHandle(toolbar, bitmap);
}

bool ToolBarInstallBitmapHandle(MfcToolBarCompat& toolbar, HBITMAP bitmap) {
    if (bitmap == nullptr) {
        return false;
    }
    BITMAP info{};
    if (GetObjectA(bitmap, sizeof(info), &info) == 0) {
        return false;
    }
    const int image_count = toolbar.image_size.cx > 0
        ? std::max<int>(1, static_cast<int>(info.bmWidth / toolbar.image_size.cx))
        : 1;
    bool accepted = true;
    if (toolbar_valid_window(toolbar)) {
        TBADDBITMAP add_bitmap{};
        add_bitmap.hInst = nullptr;
        add_bitmap.nID = reinterpret_cast<UINT_PTR>(bitmap);
        accepted = toolbar_send(toolbar, TB_ADDBITMAP,
            static_cast<WPARAM>(image_count),
            reinterpret_cast<LPARAM>(&add_bitmap)) != -1;
    }
    if (accepted) {
        if (toolbar.image_well != nullptr && toolbar.image_well != bitmap) {
            DeleteObject(toolbar.image_well);
        }
        toolbar.image_well = bitmap;
    }
    return accepted;
}

bool ToolBarSetButtons(MfcToolBarCompat& toolbar, const int* command_ids,
    int count) {
    if (count <= 0) {
        return false;
    }
    if (!toolbar_valid_window(toolbar)) {
        toolbar.count = count;
        return true;
    }

    toolbar_send(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    while (toolbar_send(toolbar, TB_BUTTONCOUNT, 0, 0) > 0) {
        if (toolbar_send(toolbar, TB_DELETEBUTTON, 0, 0) == 0) {
            return false;
        }
    }

    int image_index = 0;
    for (int index = 0; index < count; ++index) {
        const int command_id = command_ids == nullptr ? 0 : command_ids[index];
        TBBUTTON button{};
        if (command_id == 0) {
            button.fsStyle = BTNS_SEP;
            button.iBitmap = std::max<LONG>(1, toolbar.button_size.cx / 2);
        } else {
            button.iBitmap = image_index++;
            button.idCommand = command_id;
            button.fsState = TBSTATE_ENABLED;
            button.fsStyle = BTNS_BUTTON;
        }
        TBBUTTON internal = toolbar_internal_button(button);
        if (toolbar_send(toolbar, TB_ADDBUTTONSA, 1,
            reinterpret_cast<LPARAM>(&internal)) == 0) {
            return false;
        }
    }
    toolbar.count = count;
    toolbar.button_layout_dirty = true;
    ToolBarInvalidateButtonLayout(toolbar);
    return true;
}

void* ConstructToolBarLayoutItem(void* item) {
    if (item != nullptr) {
        std::memset(item, 0, sizeof(RECT) + sizeof(int) * 2);
    }
    return item;
}

void ConstructToolBarLayoutItems(void* first, std::size_t item_size,
    int count, void* (*construct)(void*)) {
    if (first == nullptr || construct == nullptr || item_size == 0) {
        return;
    }
    auto* cursor = static_cast<unsigned char*>(first);
    for (int index = 0; index < count; ++index) {
        construct(cursor + static_cast<std::size_t>(index) * item_size);
    }
}

namespace {

SIZE dialog_bar_template_pixel_size(LPCSTR template_name) {
    SIZE pixels{};
    if (template_name == nullptr) {
        return pixels;
    }

    HINSTANCE instance = GetModuleHandleA(nullptr);
    HRSRC info = FindResourceA(instance, template_name, RT_DIALOG);
    HGLOBAL resource = info == nullptr ? nullptr : LoadResource(instance, info);
    const auto* dialog_template = resource == nullptr ? nullptr :
        static_cast<const DLGTEMPLATE*>(LockResource(resource));
    if (dialog_template != nullptr) {
        SIZE units = MfcDialogTemplateDialogUnits(dialog_template);
        MfcCStringCompat face_name;
        WORD point_size = 0;
        if (DialogTemplateGetFont(dialog_template, face_name, point_size)) {
            DialogTemplateMapDialogUnits(face_name.text.c_str(), point_size,
                units.cx, units.cy, pixels);
        } else {
            const DWORD base_units = GetDialogBaseUnits();
            pixels.cx = MulDiv(units.cx, LOWORD(base_units), 4);
            pixels.cy = MulDiv(units.cy, HIWORD(base_units), 8);
        }
    }
    if (resource != nullptr) {
        UnlockResource(resource);
        FreeResource(resource);
    }
    return pixels;
}

} // namespace

MfcRuntimeClassCompat* GetDialogBarRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CDialogBar", static_cast<int>(sizeof(MfcDialogBarCompat)), 0xffff,
        +[]() -> void* {
            auto* bar = new MfcDialogBarCompat();
            ConstructDialogBar(*bar);
            return bar;
        },
        GetControlBarRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcDialogBarCompat& ConstructDialogBar(MfcDialogBarCompat& bar) {
    ConstructControlBar(bar);
    bar.runtime_class = GetDialogBarRuntimeClass();
    bar.dialog_size = SIZE{};
    bar.template_name = nullptr;
    bar.occ_dialog_info = nullptr;
    return bar;
}

bool DialogBarCreate(MfcDialogBarCompat& bar, MfcCWndCompat* parent,
    LPCSTR template_name, DWORD style, UINT id) {
    if (parent == nullptr || parent->window == nullptr) {
        CrtDbgReport(2, "bardlg.cpp", 0x45, nullptr,
            "CDialogBar::Create requires a parent window");
        return false;
    }
    if (template_name == nullptr) {
        CrtDbgReport(2, "bardlg.cpp", 0x46, nullptr,
            "CDialogBar::Create requires a dialog template");
        return false;
    }
    if (!CheckDialogTemplate(template_name, true)) {
        CrtDbgReport(2, "bardlg.cpp", 0x4b, nullptr,
            "dialog bar template must be child and initially hidden");
        ControlBarDestroyWindow(bar);
        return false;
    }

    bar.bar_style = style & 0x0040ffffU;
    bar.template_name = template_name;

    RECT rect{};
    const DWORD create_style = style | WS_CHILD;
    AfxDeferRegisterClass(2);
    AfxDeferRegisterClass(0x10);
    AfxDeferRegisterClass(0x3c000);
    if (!CreateAfxRegisteredWindow(bar, "AfxControlBar42sd", nullptr,
            create_style, rect, parent->window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr)) {
        bar.template_name = nullptr;
        return false;
    }

    CWndSetDlgCtrlID(bar, static_cast<LONG>(id));
    bar.dialog_size = dialog_bar_template_pixel_size(template_name);
    CWndModifyStyle(bar, 0, WS_CLIPSIBLINGS, 0);
    if (!ExecuteDlgInitResource(bar, reinterpret_cast<const char*>(template_name))) {
        bar.template_name = nullptr;
        return false;
    }
    CWndSetWindowPos(bar, nullptr, 0, 0, bar.dialog_size.cx,
        bar.dialog_size.cy, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    bar.template_name = nullptr;
    return true;
}

bool DialogBarCreateById(MfcDialogBarCompat& bar, MfcCWndCompat* parent,
    UINT template_id, DWORD style, UINT id) {
    return DialogBarCreate(bar, parent, MAKEINTRESOURCEA(template_id), style, id);
}

const MfcMessageMapCompat* GetDialogBarMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_INITDIALOG, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

void DestroyDialogBar(MfcDialogBarCompat& bar) {
    bar.template_name = nullptr;
    bar.occ_dialog_info = nullptr;
    bar.dialog_size = SIZE{};
    DestroyControlBar(bar);
}

MfcDialogBarCompat* DeleteDialogBarScalarDtor(MfcDialogBarCompat* bar,
    unsigned flags) {
    if (bar == nullptr) {
        return nullptr;
    }
    DestroyDialogBar(*bar);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(bar);
    }
    return bar;
}

SIZE DialogBarCalcFixedLayout(MfcDialogBarCompat& bar, bool stretch,
    bool horizontal) {
    SIZE size = bar.dialog_size;
    if (stretch) {
        if (horizontal) {
            size.cx = 0x7fff;
        } else {
            size.cy = 0x7fff;
        }
    }
    return size;
}

void DialogBarOnUpdateCmdUI(MfcDialogBarCompat& bar, MfcCWndCompat* target,
    bool disable_if_no_handler) {
    CWndUpdateDialogControls(bar, target, disable_if_no_handler);
}

bool OccCreateDialogControlsFromResource(MfcCWndCompat& window,
    LPCSTR template_name, void* occ_info, const char* failure_message) {
    if (occ_info == nullptr) {
        return true;
    }
    MfcOccManagerCompat* manager = AfxGetOccManagerCompat();
    if (manager == nullptr) {
        return true;
    }
    const bool created = manager->create_controls_from_resource != nullptr &&
        manager->create_controls_from_resource(*manager, window,
            template_name, occ_info);
    if (!created) {
        AfxTraceOutput("%s", failure_message);
    }
    return created;
}

bool OccCreateDialogControlsFromTemplate(MfcCWndCompat& window,
    const DLGTEMPLATE* dialog_template, void* occ_info,
    const char* failure_message) {
    if (occ_info == nullptr) {
        return true;
    }
    MfcOccManagerCompat* manager = AfxGetOccManagerCompat();
    if (manager == nullptr) {
        return true;
    }
    const bool created = manager->create_controls_from_template != nullptr &&
        manager->create_controls_from_template(*manager, window,
            dialog_template, occ_info);
    if (!created) {
        AfxTraceOutput("%s", failure_message);
    }
    return created;
}

bool DialogBarOnInitDialog(MfcDialogBarCompat& bar) {
    if (bar.window != nullptr && IsWindow(bar.window)) {
        DefWindowProcA(bar.window, WM_INITDIALOG, 0, 0);
    }
    if (!OccCreateDialogControlsFromResource(bar, bar.template_name,
            bar.occ_dialog_info,
            "Warning: CreateDlgControls failed during dialog bar init.\n")) {
        return false;
    }
    return true;
}

bool DialogBarSetOccDialogInfo(MfcDialogBarCompat& bar, void* occ_info) {
    bar.occ_dialog_info = occ_info;
    return true;
}

void AfxFormatStringsFromResource(MfcCStringCompat& output, UINT resource_id,
    const char* const* inserts, int count) {
    MfcCStringCompat format;
    if (!CStringLoadString(format, resource_id)) {
        AfxTraceOutput("Error: failed to load AfxFormatString resource 0x%04x.\n",
            resource_id);
        output.text.clear();
        return;
    }
    AfxFormatStrings(output, format.text.c_str(), inserts, count);
}

void AfxFormatStrings(MfcCStringCompat& output, const char* format,
    const char* const* inserts, int count) {
    output.text.clear();
    if (format == nullptr) {
        return;
    }
    if (count < 0) {
        count = 0;
    }

    const auto* cursor = reinterpret_cast<const unsigned char*>(format);
    while (*cursor != '\0') {
        int index = -1;
        if (cursor[0] == '%' &&
            ((cursor[1] >= '1' && cursor[1] <= '9') ||
                (cursor[1] >= 'A' && cursor[1] <= 'Z'))) {
            index = cursor[1] <= '9' ? cursor[1] - '1'
                                     : cursor[1] - 'A' + 9;
            cursor += 2;
            if (index < count) {
                const char* insert = inserts == nullptr ? nullptr : inserts[index];
                if (insert != nullptr) {
                    output.text += insert;
                }
            } else {
                AfxTraceOutput("Error: illegal string index requested in AfxFormatString.\n");
                output.text.push_back('?');
            }
            continue;
        }

        if (IsDBCSLeadByte(*cursor) && cursor[1] != '\0') {
            output.text.push_back(static_cast<char>(*cursor++));
        }
        output.text.push_back(static_cast<char>(*cursor++));
    }
}

void AfxFormatString1(MfcCStringCompat& output, UINT resource_id,
    const char* insert) {
    const char* inserts[1] = {insert};
    AfxFormatStringsFromResource(output, resource_id, inserts, 1);
}

void AfxFormatString2(MfcCStringCompat& output, UINT resource_id,
    const char* insert1, const char* insert2) {
    const char* inserts[2] = {insert1, insert2};
    AfxFormatStringsFromResource(output, resource_id, inserts, 2);
}

void CWndSendCloseMessage(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        AfxTraceOutput("Warning: sending WM_CLOSE to an invalid window.\n");
        return;
    }
    SendMessageA(window.window, WM_CLOSE, 0, 0);
}

MfcRuntimeClassCompat* GetDocTemplateRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CDocTemplate", static_cast<int>(sizeof(MfcDocTemplateCompat)), 0xffff,
        nullptr, GetCmdTargetRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetDocTemplateMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{GetCmdTargetMessageMap(), entries};
    return &map;
}

std::string DocTemplateStringPart(const MfcDocTemplateCompat& templ,
    int index) {
    std::size_t begin = 0;
    for (int current = 0; current < index; ++current) {
        begin = templ.doc_strings.find('\n', begin);
        if (begin == std::string::npos) {
            return {};
        }
        ++begin;
    }
    std::size_t end = templ.doc_strings.find('\n', begin);
    return templ.doc_strings.substr(begin,
        end == std::string::npos ? std::string::npos : end - begin);
}

void DocTemplateLoadMenuAndAccel(UINT resource, HMENU& menu, HACCEL& accel) {
    if (resource == 0) {
        return;
    }
    HINSTANCE instance = GetModuleHandleA(nullptr);
    if (menu == nullptr) {
        menu = LoadMenuA(instance, MAKEINTRESOURCEA(resource));
    }
    if (accel == nullptr) {
        accel = LoadAcceleratorsA(instance, MAKEINTRESOURCEA(resource));
    }
}

MfcDocTemplateCompat& ConstructDocTemplate(MfcDocTemplateCompat& templ,
    unsigned resource_id, void* doc_class, void* frame_class, void* view_class) {
    ConstructCmdTarget(templ);
    templ.runtime_class = GetDocTemplateRuntimeClass();
    templ.message_map = GetDocTemplateMessageMap();
    templ.id_resource = resource_id;
    templ.doc_class = doc_class;
    templ.frame_class = frame_class;
    templ.view_class = view_class;
    templ.auto_delete = false;
    DocTemplateLoadTemplate(templ);
    g_doc_template_registry.push_back(&templ);
    return templ;
}

void DocTemplateLoadTemplate(MfcDocTemplateCompat& templ) {
    if (templ.id_resource != 0) {
        char buffer[512]{};
        if (LoadStringA(GetModuleHandleA(nullptr), templ.id_resource, buffer,
                static_cast<int>(sizeof(buffer))) != 0) {
            templ.doc_strings = buffer;
        } else if (templ.doc_strings.empty()) {
            AfxTraceOutput("Warning: no document strings in string table for ID 0x%04x.\n",
                templ.id_resource);
        }
    }
    DocTemplateLoadMenuAndAccel(templ.container_resource,
        templ.container_menu, templ.container_accelerator);
    DocTemplateLoadMenuAndAccel(templ.server_resource, templ.server_menu,
        templ.server_accelerator);
    DocTemplateLoadMenuAndAccel(templ.ole_resource, templ.ole_menu,
        templ.ole_accelerator);
}

void DocTemplateSetServerInfo(MfcDocTemplateCompat& templ,
    unsigned server_resource, unsigned container_resource, void* frame_class,
    void* view_class) {
    templ.server_resource = server_resource;
    templ.container_resource = container_resource;
    templ.ole_frame_class = frame_class;
    templ.ole_view_class = view_class;
    DocTemplateLoadTemplate(templ);
}

void DocTemplateSetContainerInfo(MfcDocTemplateCompat& templ,
    unsigned container_resource) {
    templ.ole_resource = container_resource;
    DocTemplateLoadTemplate(templ);
}

void DestroyDocTemplate(MfcDocTemplateCompat& templ) {
    auto it = std::remove(g_doc_template_registry.begin(),
        g_doc_template_registry.end(), &templ);
    g_doc_template_registry.erase(it, g_doc_template_registry.end());
    if (templ.container_menu != nullptr) {
        DestroyMenu(templ.container_menu);
    }
    if (templ.server_menu != nullptr) {
        DestroyMenu(templ.server_menu);
    }
    if (templ.ole_menu != nullptr) {
        DestroyMenu(templ.ole_menu);
    }
    if (templ.container_accelerator != nullptr) {
        DestroyAcceleratorTable(templ.container_accelerator);
    }
    if (templ.server_accelerator != nullptr) {
        DestroyAcceleratorTable(templ.server_accelerator);
    }
    if (templ.ole_accelerator != nullptr) {
        DestroyAcceleratorTable(templ.ole_accelerator);
    }
    templ.documents.clear();
    templ.doc_strings.clear();
    DestroyCmdTarget(templ);
}

MfcDocTemplateCompat* DeleteDocTemplateScalarDtor(MfcDocTemplateCompat* templ,
    unsigned flags) {
    if (templ == nullptr) {
        return nullptr;
    }
    DestroyDocTemplate(*templ);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(templ);
    }
    return templ;
}

bool DocTemplateGetDocString(const MfcDocTemplateCompat& templ,
    std::string& out, int index) {
    out = DocTemplateStringPart(templ, index);
    return !out.empty();
}

void DocTemplateAddDocument(MfcDocTemplateCompat& templ,
    MfcDocumentCompat& document) {
    if (document.doc_template != nullptr) {
        AfxTraceOutput("Warning: document already has a template.\n");
    }
    if (std::find(templ.documents.begin(), templ.documents.end(),
        &document) == templ.documents.end()) {
        templ.documents.push_back(&document);
    }
    document.doc_template = &templ;
}

void DocTemplateRemoveDocument(MfcDocTemplateCompat& templ,
    MfcDocumentCompat& document) {
    auto it = std::remove(templ.documents.begin(), templ.documents.end(),
        &document);
    templ.documents.erase(it, templ.documents.end());
    if (document.doc_template == &templ) {
        document.doc_template = nullptr;
    }
}

int DocTemplateMatchDocType(MfcDocTemplateCompat& templ, const char* path,
    MfcDocumentCompat** matched_document) {
    if (matched_document != nullptr) {
        *matched_document = nullptr;
    }
    if (path == nullptr) {
        return 0;
    }
    char full_path[MAX_PATH]{};
    AfxFullPath(full_path, path);
    const char* compare_path = full_path[0] == '\0' ? path : full_path;
    for (MfcDocumentCompat* document : templ.documents) {
        if (document != nullptr &&
            FileNameCompare(document->path_name.c_str(), compare_path) == 0) {
            if (matched_document != nullptr) {
                *matched_document = document;
            }
            return 5;
        }
    }

    std::string extension;
    if (DocTemplateGetDocString(templ, extension, 4) && !extension.empty()) {
        const char* dot = std::strrchr(compare_path, '.');
        if (dot != nullptr && lstrcmpiA(dot, extension.c_str()) == 0) {
            return 4;
        }
    }
    return 3;
}

MfcDocumentCompat* DocTemplateCreateNewDocument(MfcDocTemplateCompat& templ) {
    MfcDocumentCompat* document = nullptr;
    auto* runtime = static_cast<MfcRuntimeClassCompat*>(templ.doc_class);
    if (runtime != nullptr && runtime->create_object != nullptr) {
        document = static_cast<MfcDocumentCompat*>(runtime->create_object());
    }
    if (document == nullptr) {
        document = new MfcDocumentCompat();
        ConstructDocument(*document);
    }
    DocTemplateAddDocument(templ, *document);
    return document;
}

MfcDocumentCompat* DocTemplateOpenDocumentFile(MfcDocTemplateCompat& templ,
    const char* path, bool make_visible) {
    MfcDocumentCompat* document = DocTemplateCreateNewDocument(templ);
    if (document == nullptr) {
        AfxTraceOutput("Warning: failed to create a document for %s.\n",
            path == nullptr ? "" : path);
        return nullptr;
    }

    bool opened = false;
    if (path == nullptr || *path == '\0') {
        opened = DocumentOnNewDocument(*document);
    } else {
        opened = DocumentOnOpenDocument(*document, path);
    }
    if (!opened) {
        DocTemplateRemoveDocument(templ, *document);
        DeleteDocumentScalarDtor(document, 1);
        return nullptr;
    }

    MfcCWndCompat* frame = DocTemplateCreateNewFrame(templ, document, nullptr);
    if (frame == nullptr) {
        DocTemplateRemoveDocument(templ, *document);
        DeleteDocumentScalarDtor(document, 1);
        return nullptr;
    }
    DocTemplateInitialUpdateFrame(templ, frame, document,
        make_visible ? 1 : 0);
    return document;
}

MfcCWndCompat* DocTemplateCreateNewFrame(MfcDocTemplateCompat& templ,
    MfcDocumentCompat* document, MfcCWndCompat* other) {
    (void)other;
    if (document == nullptr) {
        AfxTraceOutput("Warning: creating frame with no document.\n");
    }
    auto* frame = new MfcCWndCompat();
    ConstructCWnd(*frame);
    frame->class_name = "CDocTemplateFrame";
    if (document != nullptr) {
        document->doc_template = &templ;
    }
    return frame;
}

MfcCWndCompat* DocTemplateCreateOleFrame(MfcDocTemplateCompat& templ,
    HWND parent, MfcDocumentCompat* document, bool server) {
    (void)server;
    MfcCWndCompat* frame = DocTemplateCreateNewFrame(templ, document, nullptr);
    frame->window = parent;
    return frame;
}

void DocTemplateInitialUpdateFrame(MfcDocTemplateCompat& templ,
    MfcCWndCompat* frame, MfcDocumentCompat* document, int make_visible) {
    (void)templ;
    if (document != nullptr) {
        for (MfcViewCompat* view : document->views) {
            if (view != nullptr) {
                ViewOnInitialUpdate(*view);
            }
        }
    }
    if (make_visible != 0 && frame != nullptr && frame->window != nullptr) {
        ShowWindow(frame->window, SW_SHOW);
        UpdateWindow(frame->window);
    }
}

bool DocTemplateSaveAllModified(MfcDocTemplateCompat& templ) {
    for (MfcDocumentCompat* document : templ.documents) {
        if (document != nullptr && !DocumentSaveModified(*document)) {
            return false;
        }
    }
    return true;
}

void DocTemplateCloseAllDocuments(MfcDocTemplateCompat& templ,
    bool end_session) {
    (void)end_session;
    auto documents = templ.documents;
    for (MfcDocumentCompat* document : documents) {
        if (document != nullptr) {
            DocumentCloseDefault(*document);
        }
    }
}

void DocTemplateUpdateFrameCounts(MfcDocTemplateCompat& templ) {
    unsigned count = 0;
    for (MfcDocumentCompat* document : templ.documents) {
        if (document != nullptr) {
            count += static_cast<unsigned>(document->views.size());
        }
    }
    AfxTraceOutput("CDocTemplate frame/view count = %u\n", count);
}

bool DocTemplateOnCmdMsg(MfcDocTemplateCompat& templ, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    if (code == -4 && templ.owner != nullptr &&
        CmdTargetOnCmdMsg(*templ.owner, id, code, extra, handler_info)) {
        return true;
    }
    return CmdTargetOnCmdMsg(templ, id, code, extra, handler_info);
}

void DocTemplateDump(const MfcDocTemplateCompat& templ) {
    AfxTraceOutput("m_nIDResource = 0x%04x\n", templ.id_resource);
    AfxTraceOutput("m_strDocStrings = %s\n", templ.doc_strings.c_str());
    AfxTraceOutput("document count = %zu\n", templ.documents.size());
}

void DocTemplateAssertValid(const MfcDocTemplateCompat& templ) {
    AfxAssertValidObject(&templ, "doctempl.cpp", 0x1a9);
    for (const MfcDocumentCompat* document : templ.documents) {
        AfxAssertValidObject(document, "doctempl.cpp", 0x1a9);
    }
}

MfcRuntimeClassCompat* GetDocumentRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CDocument", static_cast<int>(sizeof(MfcDocumentCompat)), 0xffff,
        +[]() -> void* {
            auto* document = new MfcDocumentCompat();
            ConstructDocument(*document);
            return document;
        },
        GetCmdTargetRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetDocumentMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_COMMAND, 0, 0xe141, 0xe141, nullptr, 0, nullptr},
        {WM_COMMAND, 0, 0xe103, 0xe104, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{GetCmdTargetMessageMap(), entries};
    return &map;
}

MfcDocumentCompat& ConstructDocument(MfcDocumentCompat& document) {
    ConstructCmdTarget(document);
    document.runtime_class = GetDocumentRuntimeClass();
    document.message_map = GetDocumentMessageMap();
    document.title.clear();
    document.path_name.clear();
    document.doc_template = nullptr;
    document.views.clear();
    document.modified = false;
    document.auto_delete = true;
    document.embedded = false;
    return document;
}

void DestroyDocument(MfcDocumentCompat& document) {
    if (document.modified) {
        AfxTraceOutput("Warning: destroying an unsaved document.\n");
    }
    DocumentDeleteAllViews(document);
    if (document.doc_template != nullptr) {
        DocTemplateRemoveDocument(*document.doc_template, document);
    }
    document.title.clear();
    document.path_name.clear();
    DestroyCmdTarget(document);
}

MfcDocumentCompat* DeleteDocumentScalarDtor(MfcDocumentCompat* document,
    unsigned flags) {
    if (document == nullptr) {
        return nullptr;
    }
    DestroyDocument(*document);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(document);
    }
    return document;
}

void DocumentCloseDefault(MfcDocumentCompat& document) {
    DocumentDeleteContents(document);
    if (document.auto_delete) {
        DeleteDocumentScalarDtor(&document, 1);
    }
}

void OnCloseDocument(MfcDocumentCompat& document) {
    DocumentCloseDefault(document);
}

void DocumentDeleteContents(MfcDocumentCompat& document) {
    (void)document;
}

void DocumentDeleteAllViews(MfcDocumentCompat& document) {
    for (MfcViewCompat* view : document.views) {
        if (view != nullptr) {
            view->document = nullptr;
        }
    }
    document.views.clear();
}

void DocumentDetachViews(MfcDocumentCompat& document) {
    DocumentDeleteAllViews(document);
}

void DocumentSetTitle(MfcDocumentCompat& document, const char* title) {
    document.title = title == nullptr ? "" : title;
    DocumentUpdateFrameCounts(document);
}

void DocumentOnChangedViewList(MfcDocumentCompat& document) {
    if (!document.views.empty() || !document.auto_delete) {
        DocumentUpdateFrameCounts(document);
    } else {
        DocumentCloseDefault(document);
    }
}

void DocumentUpdateFrameCounts(MfcDocumentCompat& document) {
    AfxTraceOutput("CDocument view count = %zu\n", document.views.size());
}

bool DocumentCanCloseFrame(MfcDocumentCompat& document, MfcCWndCompat* frame) {
    (void)frame;
    return DocumentSaveModified(document);
}

void DocumentDefaultNoop() {
}

bool DocumentDefaultFalse() {
    return false;
}

bool DocumentAlternateDefaultFalse() {
    return false;
}

void DocumentSetPathName(MfcDocumentCompat& document, const char* path,
    bool add_to_mru) {
    char full_path[MAX_PATH]{};
    AfxFullPath(full_path, path == nullptr ? "" : path);
    document.path_name = full_path[0] == '\0' ? (path == nullptr ? "" : path)
        : full_path;
    document.embedded = false;
    char title[MAX_PATH]{};
    if (GetFileTitleCompat(document.path_name.c_str(), title,
            static_cast<unsigned>(sizeof(title))) == 0) {
        DocumentSetTitle(document, title);
    }
    if (add_to_mru) {
        AfxTraceOutput("Add document to MRU: %s\n", document.path_name.c_str());
    }
}

void DocumentOnFileClose(MfcDocumentCompat& document) {
    if (DocumentSaveModified(document)) {
        DocumentCloseDefault(document);
    }
}

void DocumentOnFileSave(MfcDocumentCompat& document) {
    (void)DocumentDoFileSave(document);
}

void DocumentOnFileSaveAs(MfcDocumentCompat& document) {
    if (!DocumentDoSave(document, nullptr, true)) {
        AfxTraceOutput("Warning: File save as failed.\n");
    }
}

bool DocumentDoFileSave(MfcDocumentCompat& document) {
    if (document.path_name.empty()) {
        return DocumentDoSave(document, nullptr, true);
    }
    DWORD attributes = GetFileAttributesA(document.path_name.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        return DocumentDoSave(document, nullptr, true);
    }
    return DocumentDoSave(document, document.path_name.c_str(), true);
}

bool DocumentDoSave(MfcDocumentCompat& document, const char* path,
    bool replace) {
    std::string target = path == nullptr || *path == '\0'
        ? document.path_name : path;
    if (target.empty()) {
        target = DocumentGetTempFileName(".", false);
    }
    MfcFileCompat file;
    ConstructFileDefault(file);
    MfcFileExceptionCompat exception;
    if (!DocumentFileOpenWithBackup(file, target.c_str(),
        0x1001U | 0x20U, &exception)) {
        DocumentReportSaveLoadException(target.c_str(), &exception, true,
            0xf100);
        return false;
    }
    FileClose(file);
    if (replace) {
        DocumentSetPathName(document, target.c_str(), true);
    }
    document.modified = false;
    return true;
}

bool DocumentDoSaveFailureCleanup() {
    return false;
}

bool DocumentSaveModified(MfcDocumentCompat& document) {
    if (!document.modified) {
        return true;
    }
    return DocumentDoFileSave(document);
}

void DocumentReportSaveLoadException(const char* path, void* exception,
    bool saving, unsigned default_message) {
    (void)exception;
    AfxTraceOutput("%s exception on %s, message 0x%04x.\n",
        saving ? "Saving" : "Loading", path == nullptr ? "" : path,
        default_message);
}

std::string DocumentGetTempFileName(const char* path, bool keep_original) {
    char full_path[MAX_PATH]{};
    char* file_part = nullptr;
    GetFullPathNameA(path == nullptr ? "." : path, MAX_PATH, full_path,
        &file_part);
    if (file_part != nullptr) {
        *file_part = '\0';
    }
    char temp_path[MAX_PATH]{};
    GetTempFileNameA(full_path[0] == '\0' ? "." : full_path, "MFC", 0,
        temp_path);
    if (!keep_original) {
        FileRemove(temp_path);
    }
    return temp_path;
}

bool DocumentFileOpenWithBackup(MfcFileCompat& file, const char* path,
    unsigned open_flags, MfcFileExceptionCompat* exception) {
    return FileOpen(file, path, open_flags, exception);
}

void DocumentAbortFile(MfcFileCompat& file, const std::string& temp_path) {
    FileAbort(file);
    if (!temp_path.empty()) {
        FileRemove(temp_path.c_str());
    }
}

void DocumentCommitFile(MfcFileCompat& file, std::string& path_name,
    const std::string& temp_path) {
    std::string old_path = path_name;
    FileClose(file);
    if (!temp_path.empty()) {
        if (!old_path.empty()) {
            MoveFileExA(temp_path.c_str(), old_path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        } else {
            path_name = temp_path;
        }
    }
}

MfcFileCompat* DocumentGetFile(MfcDocumentCompat& document, const char* path,
    unsigned open_flags, MfcFileExceptionCompat* exception) {
    (void)document;
    MfcFileCompat* file = nullptr;
    if ((open_flags & 0x1000U) != 0) {
        auto* mirror = new MfcMirrorFileCompat();
        ConstructMirrorFile(*mirror);
        file = mirror;
    } else {
        file = new MfcFileCompat();
        ConstructFileDefault(*file);
    }
    if (!FileOpen(*file, path, open_flags, exception)) {
        delete file;
        return nullptr;
    }
    return file;
}

void DocumentReleaseFile(MfcDocumentCompat& document, MfcFileCompat* file,
    bool abort) {
    (void)document;
    if (file == nullptr) {
        return;
    }
    if (abort) {
        FileAbort(*file);
    } else {
        FileClose(*file);
    }
    delete file;
}

bool DocumentOnNewDocument(MfcDocumentCompat& document) {
    if (!document.path_name.empty()) {
        AfxTraceOutput("Warning: OnNewDocument replaces an existing document path.\n");
    }
    DocumentDeleteContents(document);
    document.path_name.clear();
    document.modified = false;
    return true;
}

bool DocumentOnOpenDocument(MfcDocumentCompat& document, const char* path) {
    MfcFileExceptionCompat exception;
    MfcFileCompat* file = DocumentGetFile(document, path, 0x20U, &exception);
    if (file == nullptr) {
        DocumentReportSaveLoadException(path, &exception, false, 0xf101);
        return false;
    }
    DocumentDeleteContents(document);
    document.modified = true;
    MfcArchiveCompat archive;
    ConstructArchive(archive, file, 0, 0x1000, nullptr);
    DocumentSerializeNoop(document, archive);
    DestroyArchive(archive);
    DocumentReleaseFile(document, file, false);
    document.modified = false;
    DocumentSetPathName(document, path, true);
    return true;
}

bool DocumentOpenFailureCleanup() {
    return false;
}

bool DocumentOpenSuccessCleanup() {
    return true;
}

bool DocumentOnSaveDocument(MfcDocumentCompat& document, const char* path) {
    MfcFileExceptionCompat exception;
    MfcFileCompat* file = DocumentGetFile(document, path, 0x1001U | 0x20U,
        &exception);
    if (file == nullptr) {
        DocumentReportSaveLoadException(path, &exception, true, 0xf102);
        return false;
    }
    MfcArchiveCompat archive;
    ConstructArchive(archive, file, 1, 0x1000, nullptr);
    DocumentSerializeNoop(document, archive);
    DestroyArchive(archive);
    DocumentReleaseFile(document, file, false);
    document.modified = false;
    return true;
}

bool DocumentSaveFailureCleanup() {
    return false;
}

bool DocumentSaveSuccessCleanup() {
    return true;
}

void DocumentSerializeNoop(MfcDocumentCompat& document,
    MfcArchiveCompat& archive) {
    (void)document;
    (void)archive;
}

void DocumentAddView(MfcDocumentCompat& document, MfcViewCompat& view) {
    if (view.document != nullptr) {
        AfxTraceOutput("Warning: adding a view that already has a document.\n");
    }
    if (std::find(document.views.begin(), document.views.end(), &view) ==
        document.views.end()) {
        document.views.push_back(&view);
    }
    view.document = &document;
    DocumentOnChangedViewList(document);
}

void DocumentRemoveView(MfcDocumentCompat& document, MfcViewCompat& view) {
    auto it = std::remove(document.views.begin(), document.views.end(), &view);
    document.views.erase(it, document.views.end());
    if (view.document == &document) {
        view.document = nullptr;
    }
    DocumentOnChangedViewList(document);
}

MfcViewCompat* DocumentGetNextView(MfcDocumentCompat& document,
    std::size_t& position) {
    if (position >= document.views.size()) {
        return nullptr;
    }
    return document.views[position++];
}

void SendInitialUpdate(MfcDocumentCompat& document) {
    std::size_t position = 0;
    while (MfcViewCompat* view = DocumentGetNextView(document, position)) {
        ViewOnInitialUpdate(*view);
    }
}

void DocumentUpdateAllViews(MfcDocumentCompat& document, MfcViewCompat* sender,
    LPARAM hint, void* hint_object) {
    for (MfcViewCompat* view : document.views) {
        if (view != nullptr && view != sender) {
            ViewOnUpdate(*view, sender, hint, hint_object);
        }
    }
}

bool DocumentOnCmdMsg(MfcDocumentCompat& document, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    if (CmdTargetOnCmdMsg(document, id, code, extra, handler_info)) {
        return true;
    }
    if (document.doc_template != nullptr) {
        return DocTemplateOnCmdMsg(*document.doc_template, id, code, extra,
            handler_info);
    }
    return false;
}

MfcFileStatusCompat& ConstructFileStatusDefault(MfcFileStatusCompat& status) {
    status = MfcFileStatusCompat{};
    return status;
}

MfcRuntimeClassCompat* GetMirrorFileRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CMirrorFile", static_cast<int>(sizeof(MfcMirrorFileCompat)), 0xffff,
        +[]() -> void* {
            auto* file = new MfcMirrorFileCompat();
            ConstructMirrorFile(*file);
            return file;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcMirrorFileCompat& ConstructMirrorFile(MfcMirrorFileCompat& file) {
    ConstructFileDefault(file);
    file.runtime_class = GetMirrorFileRuntimeClass();
    file.temp_name.clear();
    return file;
}

MfcMirrorFileCompat* DeleteMirrorFileScalarDtor(MfcMirrorFileCompat* file,
    unsigned flags) {
    if (file == nullptr) {
        return nullptr;
    }
    DestroyMirrorFile(*file);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(file);
    }
    return file;
}

void DestroyMirrorFile(MfcMirrorFileCompat& file) {
    file.temp_name.clear();
    FileAbort(file);
}

namespace {

MfcRuntimeClassCompat* GetOleDispatchExceptionRuntimeClassCompat() {
    static MfcRuntimeClassCompat runtime_class{
        "COleDispatchException",
        static_cast<int>(sizeof(MfcOleDispatchExceptionCompat)), 0xffff,
        +[]() -> void* {
            auto* exception = new MfcOleDispatchExceptionCompat();
            ConstructSimpleException(*exception);
            exception->scode = DISP_E_EXCEPTION;
            return exception;
        },
        GetExceptionRuntimeClassCompat(), nullptr};
    return &runtime_class;
}

} // namespace

MfcRuntimeClassCompat* MfcDocOleRuntimeClassThunk_005f01b0() {
    return GetDocTemplateRuntimeClass();
}

MfcRuntimeClassCompat* MfcDocOleRuntimeClassThunk_005f01c0() {
    return GetOleDispatchExceptionRuntimeClassCompat();
}

MfcRuntimeClassCompat* MfcDocOleRuntimeClassThunk_005f01d0() {
    return GetDocumentRuntimeClass();
}

MfcRuntimeClassCompat* MfcDocOleRuntimeClassThunk_005f01e0() {
    return GetDockBarRuntimeClass();
}

MfcRuntimeClassCompat* MfcDocOleRuntimeClassThunk_005f0261() {
    return GetMiniDockFrameRuntimeClass();
}

MfcRuntimeClassCompat* MfcDocOleRuntimeClassThunk_005f02f1() {
    return GetMiniFrameRuntimeClass();
}

MfcRuntimeClassCompat* GetDocManagerRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CDocManager", static_cast<int>(sizeof(MfcDocManagerCompat)), 0xffff,
        +[]() -> void* {
            auto* manager = new MfcDocManagerCompat();
            ConstructDocManager(*manager);
            return manager;
        },
        GetCmdTargetRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetDocManagerMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{GetCmdTargetMessageMap(), entries};
    return &map;
}

MfcDocManagerCompat& ConstructDocManager(MfcDocManagerCompat& manager) {
    ConstructCmdTarget(manager);
    manager.runtime_class = GetDocManagerRuntimeClass();
    manager.message_map = GetDocManagerMessageMap();
    manager.templates.clear();
    return manager;
}

void DestroyDocManager(MfcDocManagerCompat& manager) {
    auto templates = manager.templates;
    manager.templates.clear();
    for (MfcDocTemplateCompat* templ : templates) {
        if (templ != nullptr && templ->auto_delete) {
            DeleteDocTemplateScalarDtor(templ, 1);
        }
    }
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr && app->doc_manager == &manager) {
        app->doc_manager = nullptr;
    }
    DestroyCmdTarget(manager);
}

MfcDocManagerCompat* DeleteDocManagerScalarDtor(MfcDocManagerCompat* manager,
    unsigned flags) {
    if (manager == nullptr) {
        return nullptr;
    }
    DestroyDocManager(*manager);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(manager);
    }
    return manager;
}

void DocManagerAddDocTemplate(MfcDocManagerCompat& manager,
    MfcDocTemplateCompat& templ) {
    if (std::find(manager.templates.begin(), manager.templates.end(), &templ) ==
        manager.templates.end()) {
        manager.templates.push_back(&templ);
    }
    templ.owner = &manager;
    if (std::find(g_doc_template_registry.begin(), g_doc_template_registry.end(),
        &templ) == g_doc_template_registry.end()) {
        g_doc_template_registry.push_back(&templ);
    }
}

void DocManagerRemoveDocTemplate(MfcDocManagerCompat& manager,
    MfcDocTemplateCompat& templ) {
    auto it = std::remove(manager.templates.begin(), manager.templates.end(),
        &templ);
    manager.templates.erase(it, manager.templates.end());
    if (templ.owner == &manager) {
        templ.owner = nullptr;
    }
}

std::vector<MfcDocTemplateCompat*> CollectDocManagerTemplates(
    const MfcDocManagerCompat* manager) {
    std::vector<MfcDocTemplateCompat*> templates;
    if (manager != nullptr && !manager->templates.empty()) {
        templates = manager->templates;
    } else {
        templates = g_doc_template_registry;
    }
    templates.erase(std::remove(templates.begin(), templates.end(), nullptr),
        templates.end());
    return templates;
}

void DocManagerActivateExistingDocument(MfcDocumentCompat& document) {
    if (document.views.empty()) {
        AfxTraceOutput("Error: Can not find a view for document %p.\n",
            &document);
        return;
    }
    MfcViewCompat* view = document.views.front();
    if (view == nullptr || view->window == nullptr ||
        !IsWindow(view->window)) {
        AfxTraceOutput("Error: Can not find a valid view window for document %p.\n",
            &document);
        return;
    }

    HWND frame = nullptr;
    auto* active_frame = static_cast<MfcCWndCompat*>(view->active_frame);
    if (active_frame != nullptr && active_frame->window != nullptr &&
        IsWindow(active_frame->window)) {
        frame = active_frame->window;
    } else {
        frame = GetAncestor(view->window, GA_ROOT);
    }
    if (frame != nullptr) {
        ShowWindow(frame, SW_SHOWNORMAL);
        SetActiveWindow(frame);
        BringWindowToTop(frame);
    }
    SetFocus(view->window);

    HWND main = AfxGetMainWndHandleCompat();
    if (main != nullptr && main != frame && IsWindow(main)) {
        SetActiveWindow(main);
        BringWindowToTop(main);
    }
}

MfcDocumentCompat* DocManagerOpenDocumentFile(MfcDocManagerCompat* manager,
    const char* path) {
    if (path == nullptr) {
        path = "";
    }
    if (std::strlen(path) > MAX_PATH - 1) {
        AfxTraceOutput("Warning: document path is longer than MAX_PATH: %s\n",
            path);
    }

    std::string candidate = path;
    if (!candidate.empty() && candidate.front() == '"') {
        candidate.erase(candidate.begin());
        std::size_t quote = candidate.find('"');
        if (quote != std::string::npos) {
            candidate.resize(quote);
        }
    }

    char full_path[MAX_PATH]{};
    AfxFullPath(full_path, candidate.c_str());
    if (full_path[0] != '\0') {
        candidate = full_path;
    }

    char link_target[MAX_PATH]{};
    if (ResolveShellLinkTarget(candidate.c_str(), link_target,
        static_cast<unsigned>(sizeof(link_target))) && link_target[0] != '\0') {
        candidate = link_target;
    }

    MfcDocTemplateCompat* best_template = nullptr;
    MfcDocumentCompat* matched_document = nullptr;
    int best_match = 0;
    for (MfcDocTemplateCompat* templ : CollectDocManagerTemplates(manager)) {
        MfcDocumentCompat* local_document = nullptr;
        const int match = DocTemplateMatchDocType(*templ, candidate.c_str(),
            &local_document);
        if (match > best_match) {
            best_match = match;
            best_template = templ;
            matched_document = local_document;
        }
        if (match == 5) {
            break;
        }
    }

    if (matched_document != nullptr) {
        DocManagerActivateExistingDocument(*matched_document);
        return matched_document;
    }

    if (best_template == nullptr) {
        AfxMessageBoxResource(0xf101, MB_OK, 0xffffffffU);
        return nullptr;
    }
    return DocTemplateOpenDocumentFile(*best_template, candidate.c_str(), true);
}

MfcDocumentCompat* DocManagerOpenDocumentFile(const char* path) {
    MfcDocManagerCompat* manager = nullptr;
    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr) {
        manager = static_cast<MfcDocManagerCompat*>(app->doc_manager);
    }
    return DocManagerOpenDocumentFile(manager, path);
}

int DocManagerGetOpenDocumentCount(const MfcDocManagerCompat* manager) {
    int count = 0;
    for (MfcDocTemplateCompat* templ : CollectDocManagerTemplates(manager)) {
        if (templ != nullptr) {
            count += static_cast<int>(templ->documents.size());
        }
    }
    return count;
}

int GetDocumentCount(const MfcDocManagerCompat& manager) {
    return DocManagerGetOpenDocumentCount(&manager);
}

namespace {

void append_ofn_filter_pair(std::string& filter, const std::string& display,
    const std::string& pattern) {
    filter.append(display);
    filter.push_back('\0');
    filter.append(pattern);
    filter.push_back('\0');
}

unsigned ofn_filter_pair_count(const std::string& filter) {
    unsigned nul_count = 0;
    for (char ch : filter) {
        if (ch == '\0') {
            ++nul_count;
        }
    }
    return nul_count / 2;
}

std::string load_string_or(UINT resource_id, const char* fallback) {
    char buffer[256]{};
    if (AfxLoadStringCompat(resource_id, buffer,
            static_cast<int>(sizeof(buffer))) > 0) {
        return buffer;
    }
    return fallback == nullptr ? std::string{} : std::string{fallback};
}

} // namespace

void DocManagerAppendFilterSuffix(MfcDocManagerCompat& manager,
    std::string& filter, MfcFileDialogCompat& dialog,
    MfcDocTemplateCompat& templ, std::string* default_extension) {
    (void)manager;
    std::string extension;
    std::string filter_name;
    if (!DocTemplateGetDocString(templ, extension, 4) || extension.empty()) {
        return;
    }
    if (extension.front() != '.') {
        return;
    }
    if (!DocTemplateGetDocString(templ, filter_name, 3) ||
        filter_name.empty()) {
        filter_name = extension + " Files";
    }

    const unsigned index = ofn_filter_pair_count(filter) + 1;
    if (default_extension != nullptr && default_extension->empty()) {
        *default_extension = extension.substr(1);
        dialog.ofn.nFilterIndex = index;
    }
    append_ofn_filter_pair(filter, filter_name, "*" + extension);
}

std::size_t DocManagerGetFirstDocTemplatePosition(
    const MfcDocManagerCompat& manager) {
    return manager.templates.empty() ? 0U : 1U;
}

MfcDocTemplateCompat* DocManagerGetNextDocTemplate(
    const MfcDocManagerCompat& manager, std::size_t& position) {
    if (position == 0) {
        return nullptr;
    }
    const std::size_t index = position - 1;
    if (index >= manager.templates.size()) {
        position = 0;
        return nullptr;
    }
    MfcDocTemplateCompat* templ = manager.templates[index];
    position = index + 1 < manager.templates.size() ? index + 2 : 0;
    return templ;
}

bool DocManagerSaveAllModified(MfcDocManagerCompat& manager) {
    for (MfcDocTemplateCompat* templ : manager.templates) {
        if (templ != nullptr && !DocTemplateSaveAllModified(*templ)) {
            return false;
        }
    }
    return true;
}

void DocManagerCloseAllDocuments(MfcDocManagerCompat& manager,
    bool end_session) {
    for (MfcDocTemplateCompat* templ : manager.templates) {
        if (templ != nullptr) {
            DocTemplateCloseAllDocuments(*templ, end_session);
        }
    }
}

bool DocManagerDoPromptFileName(MfcDocManagerCompat& manager,
    std::string& path, UINT title_id, DWORD flags, bool open_dialog,
    MfcDocTemplateCompat* templ) {
    MfcFileDialogCompat dialog{};
    HWND owner = AfxGetMainWndHandleCompat();
    ConstructMfcFileDialog(dialog, open_dialog, nullptr, nullptr, flags,
        nullptr, owner);
    dialog.title = load_string_or(title_id, open_dialog ? "Open" : "Save As");
    dialog.ofn.lpstrTitle = dialog.title.c_str();

    std::string filter;
    std::string default_extension;
    if (templ != nullptr) {
        DocManagerAppendFilterSuffix(manager, filter, dialog, *templ,
            &default_extension);
    } else {
        bool first = true;
        for (MfcDocTemplateCompat* candidate : manager.templates) {
            if (candidate == nullptr) {
                continue;
            }
            DocManagerAppendFilterSuffix(manager, filter, dialog, *candidate,
                first ? &default_extension : nullptr);
            first = false;
        }
    }
    append_ofn_filter_pair(filter, load_string_or(0xf002, "All Files (*.*)"),
        "*.*");
    filter.push_back('\0');

    dialog.filter_storage = filter;
    dialog.default_extension = default_extension;
    dialog.ofn.lpstrFilter = dialog.filter_storage.empty()
        ? nullptr : dialog.filter_storage.c_str();
    dialog.ofn.lpstrDefExt = dialog.default_extension.empty()
        ? nullptr : dialog.default_extension.c_str();
    if (dialog.ofn.nFilterIndex == 0) {
        dialog.ofn.nFilterIndex = 1;
    }

    const int modal_result = DoModalMfcFileDialog(dialog);
    if (modal_result != 1) {
        return false;
    }
    path = GetMfcFileDialogPathName(dialog);
    return true;
}

const MfcMessageMapCompat* GetNewTypeDlgMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_INITDIALOG, 0, 0, 0, nullptr, 0, nullptr},
        {WM_COMMAND, BN_CLICKED, IDOK, IDOK, nullptr, 0, nullptr},
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

MfcNewTypeDlgCompat& ConstructNewTypeDlg(MfcNewTypeDlgCompat& dialog,
    const std::vector<MfcDocTemplateCompat*>* templates) {
    ConstructDialogWithTemplateId(dialog, 0x7801, nullptr);
    dialog.runtime_class = GetDialogRuntimeClass();
    dialog.templates = templates;
    dialog.selected_template = nullptr;
    return dialog;
}

void DestroyNewTypeDlg(MfcNewTypeDlgCompat& dialog) {
    dialog.templates = nullptr;
    dialog.selected_template = nullptr;
    DestroyDialog(dialog);
}

MfcNewTypeDlgCompat* DeleteNewTypeDlgScalarDtor(
    MfcNewTypeDlgCompat* dialog, unsigned flags) {
    if (dialog == nullptr) {
        return nullptr;
    }
    DestroyNewTypeDlg(*dialog);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dialog);
    }
    return dialog;
}

bool NewTypeDlgOnInitDialog(MfcNewTypeDlgCompat& dialog) {
    if (dialog.templates != nullptr) {
        for (MfcDocTemplateCompat* templ : *dialog.templates) {
            if (templ != nullptr) {
                dialog.selected_template = templ;
                break;
            }
        }
    }
    return DialogOnInitDialog(dialog);
}

void NewTypeDlgOnOK(MfcNewTypeDlgCompat& dialog) {
    if (dialog.selected_template == nullptr && dialog.templates != nullptr) {
        for (MfcDocTemplateCompat* templ : *dialog.templates) {
            if (templ != nullptr) {
                dialog.selected_template = templ;
                break;
            }
        }
    }
    DialogOnOK(dialog);
}

namespace {

bool dde_starts_with(const std::string& text, const char* prefix) {
    const std::size_t length = std::strlen(prefix);
    return text.size() >= length && text.compare(0, length, prefix) == 0;
}

bool dde_consume_prefix(std::string& text, const char* prefix) {
    if (!dde_starts_with(text, prefix)) {
        return false;
    }
    text.erase(0, std::strlen(prefix));
    return true;
}

bool dde_extract_argument(std::string& text, std::string& value) {
    const std::size_t quote = text.find('"');
    if (quote == std::string::npos) {
        return false;
    }
    value.assign(text, 0, quote);
    text.erase(0, quote);
    return true;
}

bool parse_dde_command_info(const char* command,
    MfcCommandLineInfoRuntimeCompat& info) {
    if (command == nullptr) {
        return false;
    }
    std::string text = command;
    if (dde_consume_prefix(text, "[open(\"")) {
        info.shell_command = MfcShellCommandRuntime::FileOpen;
    } else if (dde_consume_prefix(text, "[print(\"")) {
        info.shell_command = MfcShellCommandRuntime::FilePrint;
    } else if (dde_consume_prefix(text, "[printto(\"")) {
        info.shell_command = MfcShellCommandRuntime::FilePrintTo;
    } else {
        return false;
    }

    if (!dde_extract_argument(text, info.file_name)) {
        return false;
    }
    if (info.shell_command != MfcShellCommandRuntime::FilePrintTo) {
        return true;
    }

    if (!dde_consume_prefix(text, "\",\"") ||
        !dde_extract_argument(text, info.printer_name)) {
        return false;
    }
    if (!dde_consume_prefix(text, "\",\"") ||
        !dde_extract_argument(text, info.driver_name)) {
        return false;
    }
    if (!dde_consume_prefix(text, "\",\"") ||
        !dde_extract_argument(text, info.port_name)) {
        return false;
    }
    return true;
}

void show_main_window_for_dde_open(MfcWinAppCompat& app) {
    if (app.main_window == nullptr || IsWindow(app.main_window) == FALSE) {
        return;
    }
    int show_command = app.command_show;
    if (show_command == -1 || show_command == SW_SHOWNORMAL) {
        show_command = IsIconic(app.main_window) != FALSE ? SW_RESTORE : SW_SHOW;
    }
    ShowWindow(app.main_window, show_command);
    if (show_command != SW_MINIMIZE) {
        SetForegroundWindow(app.main_window);
    }
}

void dispatch_dde_print_command(MfcDocManagerCompat& manager,
    MfcCommandLineInfoRuntimeCompat& info, MfcWinAppCompat* app) {
    const int before_count = DocManagerGetOpenDocumentCount(&manager);
    MfcDocumentCompat* document =
        DocManagerOpenDocumentFile(&manager, info.file_name.c_str());

    HWND main_window = app == nullptr ? nullptr : app->main_window;
    void* previous_command_info =
        app == nullptr ? nullptr : app->command_line_info;
    if (app != nullptr) {
        app->command_line_info = &info;
    }
    if (main_window != nullptr && IsWindow(main_window) != FALSE) {
        SendMessageA(main_window, WM_COMMAND, 0xe108, 0);
    } else {
        AfxTraceOutput("Warning: DDE print command has no main window.\n");
    }
    if (app != nullptr) {
        app->command_line_info = previous_command_info;
    }

    if (document != nullptr &&
        DocManagerGetOpenDocumentCount(&manager) > before_count) {
        DocumentCloseDefault(*document);
    }
    if (!AfxOleGetUserCtrl() && main_window != nullptr &&
        IsWindow(main_window) != FALSE) {
        PostMessageA(main_window, WM_CLOSE, 0, 0);
    }
}

} // namespace

bool DocManagerOnDDECommand(MfcDocManagerCompat& manager,
    const char* command) {
    MfcCommandLineInfoRuntimeCompat info{};
    MfcWinAppThreadRuntime_005ee552(&info);
    if (!parse_dde_command_info(command, info)) {
        MfcWinAppThreadRuntime_005ee5fe(&info);
        return false;
    }

    MfcWinAppCompat* app = AfxGetAppCompat();
    void* previous_command_info =
        app == nullptr ? nullptr : app->command_line_info;
    if (info.shell_command == MfcShellCommandRuntime::FileOpen) {
        if (app != nullptr) {
            show_main_window_for_dde_open(*app);
        }
        DocManagerOpenDocumentFile(&manager, info.file_name.c_str());
        if (!AfxOleGetUserCtrl()) {
            AfxOleSetUserCtrl(true);
        }
        if (app != nullptr) {
            app->command_show = -1;
            app->command_line_info = previous_command_info;
        }
        MfcWinAppThreadRuntime_005ee5fe(&info);
        return true;
    }

    dispatch_dde_print_command(manager, info, app);
    if (app != nullptr) {
        app->command_line_info = previous_command_info;
    }
    MfcWinAppThreadRuntime_005ee5fe(&info);
    return true;
}

void DocManagerOnFileNew(MfcDocManagerCompat& manager) {
    std::vector<MfcDocTemplateCompat*> templates =
        CollectDocManagerTemplates(&manager);
    if (templates.empty()) {
        AfxTraceOutput("Error: no document templates registered with CWinApp.\n");
        AfxMessageBoxResource(0xf104, MB_OK, 0xffffffffU);
        return;
    }

    MfcDocTemplateCompat* selected = templates.front();
    if (templates.size() > 1) {
        MfcNewTypeDlgCompat dialog{};
        ConstructNewTypeDlg(dialog, &templates);
        NewTypeDlgOnInitDialog(dialog);
        selected = dialog.selected_template;
        DestroyNewTypeDlg(dialog);
    }
    if (selected != nullptr) {
        DocTemplateOpenDocumentFile(*selected, nullptr, true);
    }
}

void DocManagerOnFileOpen(MfcDocManagerCompat& manager) {
    std::string path;
    if (DocManagerDoPromptFileName(manager, path, 0xf000,
            OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, true, nullptr)) {
        DocManagerOpenDocumentFile(&manager, path.c_str());
    }
}

void WinAppOnFileNew(MfcWinAppCompat& app) {
    if (app.doc_manager != nullptr) {
        DocManagerOnFileNew(*static_cast<MfcDocManagerCompat*>(app.doc_manager));
    }
}

void WinAppOnFileOpen(MfcWinAppCompat& app) {
    if (app.doc_manager == nullptr) {
        CrtDbgReport(2, "appdlg.cpp", 0x23, nullptr,
            "CWinApp::OnFileOpen requires a document manager");
        return;
    }
    DocManagerOnFileOpen(*static_cast<MfcDocManagerCompat*>(app.doc_manager));
}

bool WinAppDoPromptFileNameDialog(MfcWinAppCompat& app, std::string& path,
    UINT title_id, DWORD flags, bool open_dialog, MfcDocTemplateCompat* templ) {
    if (app.doc_manager == nullptr) {
        CrtDbgReport(2, "appdlg.cpp", 0x2c, nullptr,
            "CWinApp::DoPromptFileName requires a document manager");
        return false;
    }
    return DocManagerDoPromptFileName(
        *static_cast<MfcDocManagerCompat*>(app.doc_manager), path, title_id,
        flags, open_dialog, templ);
}

void* GetThreadStateCurrentWindowSlot() {
    HWND window = AfxGetMainWndHandleCompat();
    return window == nullptr ? nullptr : CWndFromHandle(window);
}

void* GetThreadStateRoutingFrameSlot() {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    HWND window = thread == nullptr ? nullptr : thread->active_window;
    return window == nullptr ? nullptr : CWndFromHandle(window);
}

bool CWndEnableToolTips(MfcCWndCompat& window, int flags) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    (void)flags;
    return true;
}

bool CWndFilterToolTipMessage(MfcCWndCompat& window, MSG& message) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    MfcToolTipCtrlCompat tooltip{};
    tooltip.window = nullptr;
    (void)tooltip;
    (void)message;
    return false;
}

void FillInToolInfo(TOOLINFOA& tool_info, const MfcCWndCompat* window,
    UINT_PTR tool_id) {
    std::memset(&tool_info, 0, sizeof(tool_info));
    tool_info.cbSize = sizeof(tool_info);
    HWND hwnd = window == nullptr ? nullptr : window->window;
    if (tool_id == 0) {
        tool_info.hwnd = hwnd == nullptr ? nullptr : GetParent(hwnd);
        tool_info.uFlags = TTF_IDISHWND;
        tool_info.uId = reinterpret_cast<UINT_PTR>(hwnd);
    } else {
        tool_info.hwnd = hwnd;
        tool_info.uFlags = 0;
        tool_info.uId = tool_id;
    }
}

void CWndDragAcceptFiles(MfcCWndCompat& window, BOOL accept) {
    if (window.window != nullptr && IsWindow(window.window)) {
        DragAcceptFiles(window.window, accept);
    }
}

bool CWndSubclassWindow(MfcCWndCompat& window, HWND handle) {
    if (!AttachCWndHandle(window, handle)) {
        return false;
    }
    CWndPreSubclassWindowDefault(window);
    WNDPROC old_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(handle,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GetAfxWndProc())));
    if (old_proc == GetAfxWndProc()) {
        DetachCWndHandle(window);
        return false;
    }
    if (window.original_wnd_proc == nullptr) {
        window.original_wnd_proc = old_proc;
    } else if (window.original_wnd_proc != old_proc) {
        AfxTraceOutput("Error: trying to use SubclassWindow with different window procedures.\n");
        SetWindowLongPtrA(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(old_proc));
        DetachCWndHandle(window);
        return false;
    }
    return true;
}

bool CWndSubclassDlgItem(MfcCWndCompat& window, UINT control_id,
    MfcCWndCompat& parent) {
    if (parent.window == nullptr || !IsWindow(parent.window)) {
        return false;
    }
    HWND child = GetDlgItem(parent.window, static_cast<int>(control_id));
    if (child == nullptr) {
        return false;
    }
    return CWndSubclassWindow(window, child);
}

HWND CWndUnsubclassWindow(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return nullptr;
    }
    HWND handle = window.window;
    if (window.original_wnd_proc != nullptr) {
        SetWindowLongPtrA(handle, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(window.original_wnd_proc));
    }
    DetachCWndHandle(window);
    return handle;
}

MfcCWndCompat* DeleteCWndScalarDtor(MfcCWndCompat* window, unsigned flags) {
    if (window == nullptr) {
        return nullptr;
    }
    DestroyCWndCompat(*window);
    if ((flags & 1U) != 0) {
        delete window;
    }
    return window;
}

MfcMenuHandleMapCompat* GetMenuHandleMap(bool create) {
    if (!g_menu_handle_map_created && !create) {
        return nullptr;
    }
    g_menu_handle_map_created = true;
    return &g_menu_handle_map;
}

MfcMenuCompat* CMenuFromHandle(HMENU menu) {
    if (menu == nullptr) {
        return nullptr;
    }
    MfcMenuHandleMapCompat* map = GetMenuHandleMap(true);
    auto permanent = map->permanent.find(menu);
    if (permanent != map->permanent.end()) {
        return permanent->second;
    }
    auto temporary = map->temporary.find(menu);
    if (temporary != map->temporary.end()) {
        return temporary->second;
    }

    auto wrapper = std::make_unique<MfcMenuCompat>();
    wrapper->runtime_class = GetCObjectRuntimeClass();
    wrapper->menu = menu;
    wrapper->temporary = true;
    MfcMenuCompat* raw = wrapper.get();
    g_temporary_menus.push_back(std::move(wrapper));
    map->temporary[menu] = raw;
    return raw;
}

MfcMenuCompat* CMenuFromHandlePermanent(HMENU menu) {
    MfcMenuHandleMapCompat* map = GetMenuHandleMap(false);
    if (map == nullptr || menu == nullptr) {
        return nullptr;
    }
    auto permanent = map->permanent.find(menu);
    return permanent == map->permanent.end() ? nullptr : permanent->second;
}

void CMenuAssertValid(const MfcMenuCompat& menu) {
    if (menu.menu != nullptr && IsMenu(menu.menu) == FALSE) {
        AfxTraceOutput("Invalid CMenu handle %p\n", menu.menu);
    }
}

bool AttachMenuHandle(MfcMenuCompat& menu, HMENU handle) {
    if (menu.menu != nullptr || handle == nullptr) {
        return false;
    }
    MfcMenuHandleMapCompat* map = GetMenuHandleMap(true);
    if (map->permanent.find(handle) != map->permanent.end()) {
        return false;
    }
    menu.runtime_class = GetCObjectRuntimeClass();
    menu.menu = handle;
    menu.temporary = false;
    map->temporary.erase(handle);
    map->permanent[handle] = &menu;
    return true;
}

MfcMenuCompat& ConstructMenuCompat(MfcMenuCompat& menu) {
    menu.runtime_class = GetCObjectRuntimeClass();
    menu.menu = nullptr;
    menu.temporary = false;
    return menu;
}

void DestroyMenuCompat(MfcMenuCompat& menu) {
    if (menu.menu != nullptr) {
        if (MfcMenuHandleMapCompat* map = GetMenuHandleMap(false)) {
            map->permanent.erase(menu.menu);
            map->temporary.erase(menu.menu);
        }
        ::DestroyMenu(menu.menu);
    }
    menu.menu = nullptr;
    menu.temporary = false;
}

MfcMenuCompat* DeleteMenuScalarDtor(MfcMenuCompat* menu, unsigned flags) {
    if (menu == nullptr) {
        return nullptr;
    }
    DestroyMenuCompat(*menu);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(menu);
    }
    return menu;
}

HMENU MenuGetSafeHandle(const MfcMenuCompat* menu) {
    return menu == nullptr ? nullptr : menu->menu;
}

HMENU MenuGetSafeHandleInline(const MfcMenuCompat* menu) {
    return MenuGetSafeHandle(menu);
}

bool MenuEquals(const MfcMenuCompat& menu, const MfcMenuCompat* other) {
    return menu.menu == MenuGetSafeHandle(other);
}

bool MenuEqualsInline(const MfcMenuCompat& menu,
    const MfcMenuCompat* other) {
    return MenuEquals(menu, other);
}

bool MenuNotEquals(const MfcMenuCompat& menu, const MfcMenuCompat* other) {
    return !MenuEquals(menu, other);
}

bool MenuNotEqualsInline(const MfcMenuCompat& menu,
    const MfcMenuCompat* other) {
    return MenuNotEquals(menu, other);
}

HMENU MenuOperatorHandleInline(const MfcMenuCompat& menu) {
    return MenuGetSafeHandle(&menu);
}

bool MenuCreate(MfcMenuCompat& menu) {
    return AttachMenuHandle(menu, CreateMenu());
}

bool MenuCreateInline(MfcMenuCompat& menu) {
    return MenuCreate(menu);
}

bool MenuCreatePopup(MfcMenuCompat& menu) {
    return AttachMenuHandle(menu, CreatePopupMenu());
}

bool MenuCreatePopupInline(MfcMenuCompat& menu) {
    return MenuCreatePopup(menu);
}

bool MenuLoad(MfcMenuCompat& menu, LPCSTR resource_name) {
    return AttachMenuHandle(menu, LoadMenuA(AfxGetResourceHandleCompat(),
        resource_name));
}

bool MenuLoadInline(MfcMenuCompat& menu, LPCSTR resource_name) {
    return MenuLoad(menu, resource_name);
}

bool MenuLoadResourceId(MfcMenuCompat& menu, UINT resource_id) {
    return MenuLoad(menu, MAKEINTRESOURCEA(resource_id));
}

bool MenuLoadResourceIdInline(MfcMenuCompat& menu, UINT resource_id) {
    return MenuLoadResourceId(menu, resource_id);
}

bool MenuLoadIndirect(MfcMenuCompat& menu, const MENUTEMPLATEA* templ) {
    return AttachMenuHandle(menu, LoadMenuIndirectA(templ));
}

bool MenuLoadIndirectInline(MfcMenuCompat& menu,
    const MENUTEMPLATEA* templ) {
    return MenuLoadIndirect(menu, templ);
}

BOOL MenuSetContextHelpId(MfcMenuCompat& menu, DWORD context_id) {
    return menu.menu == nullptr ? FALSE : SetMenuContextHelpId(menu.menu,
        context_id);
}

DWORD MenuGetContextHelpId(const MfcMenuCompat& menu) {
    return menu.menu == nullptr ? 0 : GetMenuContextHelpId(menu.menu);
}

DWORD MenuGetContextHelpIdInline(const MfcMenuCompat& menu) {
    return MenuGetContextHelpId(menu);
}

BOOL MenuCheckRadioItem(MfcMenuCompat& menu, UINT first, UINT last,
    UINT check, UINT flags) {
    return menu.menu == nullptr ? FALSE
        : CheckMenuRadioItem(menu.menu, first, last, check, flags);
}

BOOL MenuDelete(MfcMenuCompat& menu, UINT position, UINT flags) {
    return menu.menu == nullptr ? FALSE : DeleteMenu(menu.menu, position, flags);
}

BOOL MenuDeleteInline(MfcMenuCompat& menu, UINT position, UINT flags) {
    return MenuDelete(menu, position, flags);
}

BOOL MenuAppend(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    LPCSTR item) {
    return menu.menu == nullptr ? FALSE : AppendMenuA(menu.menu, flags, id,
        item);
}

BOOL MenuAppendInline(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    LPCSTR item) {
    return MenuAppend(menu, flags, id, item);
}

BOOL MenuAppendString(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    const MfcCStringCompat& item) {
    return MenuAppend(menu, flags | MF_STRING, id, item.text.c_str());
}

BOOL MenuAppendBitmap(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    HBITMAP bitmap) {
    return menu.menu == nullptr ? FALSE
        : AppendMenuA(menu.menu, flags | MF_BITMAP, id,
            reinterpret_cast<LPCSTR>(bitmap));
}

BOOL MenuAppendBitmapInline(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    const MfcGdiObjectCompat* bitmap) {
    return MenuAppendBitmap(menu, flags, id, BitmapGetSafeHandle(bitmap));
}

UINT MenuCheckItem(MfcMenuCompat& menu, UINT id_check_item, UINT check) {
    return menu.menu == nullptr ? static_cast<UINT>(-1)
        : CheckMenuItem(menu.menu, id_check_item, check);
}

UINT MenuCheckItemInline(MfcMenuCompat& menu, UINT id_check_item,
    UINT check) {
    return MenuCheckItem(menu, id_check_item, check);
}

UINT MenuEnableItem(MfcMenuCompat& menu, UINT id_enable_item, UINT enable) {
    return menu.menu == nullptr ? static_cast<UINT>(-1)
        : EnableMenuItem(menu.menu, id_enable_item, enable);
}

UINT MenuEnableItemInline(MfcMenuCompat& menu, UINT id_enable_item,
    UINT enable) {
    return MenuEnableItem(menu, id_enable_item, enable);
}

BOOL MenuSetDefaultItem(MfcMenuCompat& menu, UINT item, UINT by_position) {
    return menu.menu == nullptr ? FALSE : SetMenuDefaultItem(menu.menu, item,
        by_position);
}

BOOL MenuSetDefaultItemInline(MfcMenuCompat& menu, UINT item,
    UINT by_position) {
    return MenuSetDefaultItem(menu, item, by_position);
}

UINT MenuGetDefaultItem(MfcMenuCompat& menu, UINT gmdi_flags,
    BOOL by_position) {
    return menu.menu == nullptr ? static_cast<UINT>(-1)
        : GetMenuDefaultItem(menu.menu, by_position, gmdi_flags);
}

UINT MenuGetDefaultItemInline(MfcMenuCompat& menu, UINT gmdi_flags,
    BOOL by_position) {
    return MenuGetDefaultItem(menu, gmdi_flags, by_position);
}

int MenuGetItemCount(const MfcMenuCompat& menu) {
    return menu.menu == nullptr ? -1 : GetMenuItemCount(menu.menu);
}

int MenuGetItemCountInline(const MfcMenuCompat& menu) {
    return MenuGetItemCount(menu);
}

UINT MenuGetItemID(const MfcMenuCompat& menu, int position) {
    return menu.menu == nullptr ? static_cast<UINT>(-1)
        : GetMenuItemID(menu.menu, position);
}

UINT MenuGetItemIDInline(const MfcMenuCompat& menu, int position) {
    return MenuGetItemID(menu, position);
}

UINT MenuGetState(const MfcMenuCompat& menu, UINT id, UINT flags) {
    return menu.menu == nullptr ? static_cast<UINT>(-1)
        : GetMenuState(menu.menu, id, flags);
}

UINT MenuGetStateInline(const MfcMenuCompat& menu, UINT id, UINT flags) {
    return MenuGetState(menu, id, flags);
}

int MenuGetString(const MfcMenuCompat& menu, UINT id, char* buffer,
    int max_count, UINT flags) {
    return menu.menu == nullptr ? 0
        : GetMenuStringA(menu.menu, id, buffer, max_count, flags);
}

int MenuGetStringInline(const MfcMenuCompat& menu, UINT id, char* buffer,
    int max_count, UINT flags) {
    return MenuGetString(menu, id, buffer, max_count, flags);
}

std::string MenuGetStringValue(const MfcMenuCompat& menu, UINT id,
    UINT flags) {
    const int length = MenuGetString(menu, id, nullptr, 0, flags);
    if (length <= 0) {
        return {};
    }
    std::string value(static_cast<std::size_t>(length) + 1U, '\0');
    int copied = MenuGetString(menu, id, value.data(),
        static_cast<int>(value.size()), flags);
    value.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return value;
}

int MenuGetStringCStringInline(const MfcMenuCompat& menu, UINT id,
    MfcCStringCompat& value, UINT flags) {
    const int length = MenuGetString(menu, id, nullptr, 0, flags);
    if (length <= 0) {
        value.text.clear();
        return length;
    }
    std::string buffer(static_cast<std::size_t>(length) * 2U + 2U, '\0');
    const int copied = MenuGetString(menu, id, buffer.data(),
        static_cast<int>(buffer.size()), flags);
    value.text.assign(buffer.data(),
        copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return length;
}

BOOL MenuGetItemInfo(const MfcMenuCompat& menu, UINT item, BOOL by_position,
    MENUITEMINFOA& info) {
    if (menu.menu == nullptr) {
        return FALSE;
    }
    if (info.cbSize == 0) {
        info.cbSize = sizeof(info);
    }
    return GetMenuItemInfoA(menu.menu, item, by_position, &info);
}

BOOL MenuGetItemInfoInline(const MfcMenuCompat& menu, UINT item,
    MENUITEMINFOA& info, BOOL by_position) {
    return MenuGetItemInfo(menu, item, by_position, info);
}

MfcMenuCompat* MenuGetSubMenu(const MfcMenuCompat& menu, int position) {
    if (menu.menu == nullptr) {
        return nullptr;
    }
    return CMenuFromHandle(GetSubMenu(menu.menu, position));
}

MfcMenuCompat* MenuGetSubMenuInline(const MfcMenuCompat& menu, int position) {
    return MenuGetSubMenu(menu, position);
}

BOOL MenuInsert(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item) {
    return menu.menu == nullptr ? FALSE
        : InsertMenuA(menu.menu, position, flags, id, item);
}

BOOL MenuInsertInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item) {
    return MenuInsert(menu, position, flags, id, item);
}

BOOL MenuInsertString(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcCStringCompat& item) {
    return MenuInsert(menu, position, flags | MF_STRING, id, item.text.c_str());
}

BOOL MenuInsertBitmap(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, HBITMAP bitmap) {
    return menu.menu == nullptr ? FALSE
        : InsertMenuA(menu.menu, position, flags | MF_BITMAP, id,
            reinterpret_cast<LPCSTR>(bitmap));
}

BOOL MenuInsertBitmapInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcGdiObjectCompat* bitmap) {
    return MenuInsertBitmap(menu, position, flags, id,
        BitmapGetSafeHandle(bitmap));
}

BOOL MenuModify(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item) {
    return menu.menu == nullptr ? FALSE
        : ModifyMenuA(menu.menu, position, flags, id, item);
}

BOOL MenuModifyInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item) {
    return MenuModify(menu, position, flags, id, item);
}

BOOL MenuModifyString(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcCStringCompat& item) {
    return MenuModify(menu, position, flags | MF_STRING, id, item.text.c_str());
}

BOOL MenuModifyBitmap(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, HBITMAP bitmap) {
    return menu.menu == nullptr ? FALSE
        : ModifyMenuA(menu.menu, position, flags | MF_BITMAP, id,
            reinterpret_cast<LPCSTR>(bitmap));
}

BOOL MenuModifyBitmapInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcGdiObjectCompat* bitmap) {
    return MenuModifyBitmap(menu, position, flags, id,
        BitmapGetSafeHandle(bitmap));
}

BOOL MenuRemove(MfcMenuCompat& menu, UINT position, UINT flags) {
    return menu.menu == nullptr ? FALSE : RemoveMenu(menu.menu, position, flags);
}

BOOL MenuRemoveInline(MfcMenuCompat& menu, UINT position, UINT flags) {
    return MenuRemove(menu, position, flags);
}

BOOL MenuSetItemBitmaps(MfcMenuCompat& menu, UINT position, UINT flags,
    HBITMAP unchecked_bitmap, HBITMAP checked_bitmap) {
    return menu.menu == nullptr ? FALSE
        : SetMenuItemBitmaps(menu.menu, position, flags, unchecked_bitmap,
            checked_bitmap);
}

BOOL MenuSetItemBitmapsInline(MfcMenuCompat& menu, UINT position, UINT flags,
    const MfcGdiObjectCompat* unchecked_bitmap,
    const MfcGdiObjectCompat* checked_bitmap) {
    return MenuSetItemBitmaps(menu, position, flags,
        BitmapGetSafeHandle(unchecked_bitmap),
        BitmapGetSafeHandle(checked_bitmap));
}

void CMenuDrawItemDefault() {
}

void CMenuMeasureItemDefault() {
}

void AfxLockTempMapsCompat() {
    ++g_temp_map_lock_count;
}

bool AfxUnlockTempMaps(bool delete_temporary) {
    if (g_temp_map_lock_count > 0) {
        --g_temp_map_lock_count;
    }
    if (g_temp_map_lock_count == 0 && delete_temporary) {
        DeleteTempHwndMap();
        DeleteTempMenuMap();
        DeleteTempImageListMap();
        DeleteTempDcMap();
        DeleteTempGdiObjectMap();
    }
    return g_temp_map_lock_count != 0;
}

MfcHandleMapCompat& ConstructHandleMap(MfcHandleMapCompat& map,
    int handle_offset, MfcRuntimeClassCompat* runtime_class, int handle_count) {
    map.permanent.clear();
    map.temporary.clear();
    map.handle_offset = handle_offset;
    map.runtime_class = runtime_class;
    map.handle_count = handle_count == 2 ? 2 : 1;
    return map;
}

void* HandleMapFromHandle(MfcHandleMapCompat& map, void* handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    auto permanent = map.permanent.find(handle);
    if (permanent != map.permanent.end()) {
        return permanent->second;
    }
    auto temporary = map.temporary.find(handle);
    if (temporary != map.temporary.end()) {
        return temporary->second;
    }
    if (map.runtime_class == nullptr) {
        return nullptr;
    }
    void* object = RuntimeClassCreateObject(*map.runtime_class);
    if (object == nullptr) {
        ThrowMfcMemoryException();
    }
    map.temporary[handle] = object;
    return HandleMapFromHandleEpilogue(object, handle, map.handle_offset,
        map.handle_count);
}

void* HandleMapFromHandleEpilogue(void* object, void* handle,
    int handle_offset, int handle_count) {
    if (object != nullptr && handle_offset >= 0) {
        auto* handle_slot = reinterpret_cast<void**>(
            static_cast<unsigned char*>(object) + handle_offset);
        handle_slot[0] = handle;
        if (handle_count == 2) {
            handle_slot[1] = handle;
        }
    }
    return object;
}

void HandleMapRemoveHandle(MfcHandleMapCompat& map, void* handle) {
    if (handle == nullptr) {
        return;
    }
    map.permanent.erase(handle);
    map.temporary.erase(handle);
}

void DeleteTempMap(MfcHandleMapCompat& map) {
    map.temporary.clear();
}

void DeleteTempHwndMap() {
    if (g_window_handle_map_created) {
        g_window_handle_map.temporary.clear();
    }
    g_temporary_windows.clear();
}

void DeleteTempImageListMap() {
    DeleteTempImageListHandleMapEntries();
}

void DeleteTempDcMap() {
    for (const auto& dc : g_temporary_dcs) {
        if (dc == nullptr) {
            continue;
        }
        if (dc->output_dc != nullptr) {
            g_dc_handle_map.erase(dc->output_dc);
        }
        if (dc->attribute_dc != nullptr && dc->attribute_dc != dc->output_dc) {
            g_dc_handle_map.erase(dc->attribute_dc);
        }
        dc->output_dc = nullptr;
        dc->attribute_dc = nullptr;
    }
    g_temporary_dcs.clear();
}

void DeleteTempGdiObjectMap() {
    if (g_gdi_object_handle_map_created) {
        g_gdi_object_handle_map.temporary.clear();
    }
    g_temporary_gdi_objects.clear();
}

void DeleteTempMenuMap() {
    if (g_menu_handle_map_created) {
        g_menu_handle_map.temporary.clear();
    }
    g_temporary_menus.clear();
}

void CWndOleControlSiteScroll(MfcCWndCompat& window, int dx, int dy,
    const RECT* scroll_rect, const RECT* clip_rect) {
    (void)window;
    (void)dx;
    (void)dy;
    (void)scroll_rect;
    (void)clip_rect;
}

void CWndCheckDlgButton(MfcCWndCompat& window, int control_id, UINT check) {
    if (window.window != nullptr && IsWindow(window.window)) {
        CheckDlgButton(window.window, control_id, check);
    }
}

void CWndCheckRadioButton(MfcCWndCompat& window, int first_id, int last_id,
    int check_id) {
    if (window.window != nullptr && IsWindow(window.window)) {
        CheckRadioButton(window.window, first_id, last_id, check_id);
    }
}

MfcCWndCompat* CWndGetDlgItem(MfcCWndCompat& window, int control_id) {
    HWND child = CWndGetDlgItemHandle(window, control_id);
    return child == nullptr ? nullptr : CWndFromHandle(child);
}

HWND CWndGetDlgItemHandle(MfcCWndCompat& window, int control_id) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return nullptr;
    }
    return GetDlgItem(window.window, control_id);
}

UINT CWndGetDlgItemInt(MfcCWndCompat& window, int control_id, BOOL* translated,
    BOOL signed_value) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (translated != nullptr) {
            *translated = FALSE;
        }
        return 0;
    }
    return GetDlgItemInt(window.window, control_id, translated, signed_value);
}

UINT CWndGetDlgItemText(MfcCWndCompat& window, int control_id, char* buffer,
    int max_count) {
    if (window.window == nullptr || !IsWindow(window.window) || buffer == nullptr) {
        return 0;
    }
    return static_cast<UINT>(GetDlgItemTextA(window.window, control_id, buffer,
        max_count));
}

LRESULT CWndSendDlgItemMessage(MfcCWndCompat& window, int control_id,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return 0;
    }
    return SendDlgItemMessageA(window.window, control_id, message, wparam,
        lparam);
}

void CWndSetDlgItemInt(MfcCWndCompat& window, int control_id, UINT value,
    BOOL signed_value) {
    if (window.window != nullptr && IsWindow(window.window)) {
        SetDlgItemInt(window.window, control_id, value, signed_value);
    }
}

void CWndSetDlgItemText(MfcCWndCompat& window, int control_id, const char* text) {
    if (window.window != nullptr && IsWindow(window.window)) {
        SetDlgItemTextA(window.window, control_id, text == nullptr ? "" : text);
    }
}

UINT CWndIsDlgButtonChecked(MfcCWndCompat& window, int control_id) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return BST_UNCHECKED;
    }
    return IsDlgButtonChecked(window.window, control_id);
}

int CWndScrollWindowExCompat(MfcCWndCompat& window, int dx, int dy,
    const RECT* scroll_rect, const RECT* clip_rect, HRGN update_region,
    RECT* update_rect, UINT flags) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return ERROR;
    }
    return ScrollWindowEx(window.window, dx, dy, scroll_rect, clip_rect,
        update_region, update_rect, flags);
}

bool CWndIsDialogMessageCompat(MfcCWndCompat& window, MSG& message) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    return IsDialogMessageA(window.window, &message) != 0;
}

LONG CWndGetStyle(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return 0;
    }
    return GetWindowLongA(window.window, GWL_STYLE);
}

LONG CWndGetExStyle(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return 0;
    }
    return GetWindowLongA(window.window, GWL_EXSTYLE);
}

bool CWndModifyStyle(MfcCWndCompat& window, DWORD remove_bits,
    DWORD add_bits, UINT flags) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    return ModifyWindowLongStyle(window.window, GWL_STYLE, remove_bits,
        add_bits, flags);
}

bool CWndModifyStyleEx(MfcCWndCompat& window, DWORD remove_bits,
    DWORD add_bits, UINT flags) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return false;
    }
    return ModifyWindowLongStyle(window.window, GWL_EXSTYLE, remove_bits,
        add_bits, flags);
}

void CWndSetWindowText(MfcCWndCompat& window, const char* text) {
    if (window.window != nullptr && IsWindow(window.window)) {
        SetWindowTextA(window.window, text == nullptr ? "" : text);
    }
}

int CWndGetWindowText(MfcCWndCompat& window, char* buffer, int max_count) {
    if (window.window == nullptr || !IsWindow(window.window) || buffer == nullptr) {
        return 0;
    }
    return GetWindowTextA(window.window, buffer, max_count);
}

int CWndGetWindowTextLength(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return 0;
    }
    return GetWindowTextLengthA(window.window);
}

int CWndGetDlgCtrlID(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return 0;
    }
    return GetDlgCtrlID(window.window);
}

void CWndSetDlgCtrlID(MfcCWndCompat& window, LONG id) {
    if (window.window != nullptr && IsWindow(window.window)) {
        SetWindowLongA(window.window, GWL_ID, id);
    }
}

void CWndMoveWindow(MfcCWndCompat& window, int x, int y, int width,
    int height, BOOL repaint) {
    if (window.window != nullptr && IsWindow(window.window)) {
        MoveWindow(window.window, x, y, width, height, repaint);
    }
}

void CWndSetWindowPos(MfcCWndCompat& window, HWND insert_after, int x, int y,
    int width, int height, UINT flags) {
    if (window.window != nullptr && IsWindow(window.window)) {
        SetWindowPos(window.window, insert_after, x, y, width, height, flags);
    }
}

void CWndShowWindow(MfcCWndCompat& window, int command) {
    if (window.window != nullptr && IsWindow(window.window)) {
        ShowWindow(window.window, command);
    }
}

BOOL CWndIsWindowEnabled(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return FALSE;
    }
    return IsWindowEnabled(window.window);
}

BOOL CWndEnableWindow(MfcCWndCompat& window, BOOL enable) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return FALSE;
    }
    return EnableWindow(window.window, enable);
}

MfcCWndCompat* CWndSetFocus(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        return nullptr;
    }
    HWND previous = SetFocus(window.window);
    return previous == nullptr ? nullptr : CWndFromHandle(previous);
}

void AfxWin2ControlInline_005e0645(MfcCWndCompat& window) {
    CWndEnableWindow(window, FALSE);
}

void AfxWin2ControlInline_005e065f(MfcCWndCompat& window) {
    CWndEnableWindow(window, TRUE);
}

MfcCStringCompat& AfxWin2ControlInline_005e06fe(
    MfcCStringCompat& text, const char* source) {
    return AssignCStringAnsi(text, source);
}

void AfxWin2ControlInline_005e0679(MfcFrameWndCompat& frame) {
    frame.idle_update_flags |= 0x02;
}

void AfxWin2ControlInline_005e0699(MfcFrameWndCompat& frame, BOOL update_now) {
    frame.idle_update_flags |= (update_now != FALSE ? 0x04U : 0U) | 0x08U;
}

void AfxWin2ControlInline_005e06e2(MfcFrameWndCompat& frame, void* control_bar) {
    frame.control_bars.push_back(control_bar);
}

MfcCStringCompat& AfxWin2ControlInline_005e071d(
    MfcFrameWndCompat& frame, MfcCStringCompat& destination) {
    MfcCStringCompat frame_title;
    frame_title.text = frame.title;
    return ConstructCStringCopy(destination, frame_title);
}

bool AfxWin2ControlInline_005e0750(MfcDialogCompat& dialog,
    UINT template_id, MfcCWndCompat* parent) {
    return DialogCreate(dialog, MAKEINTRESOURCEA(template_id & 0xffffU),
        parent);
}

void AfxWin2ControlInline_005e0773(MfcCWndCompat& window, RECT* rect) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x221)) {
            CrtDebugBreak();
        }
        return;
    }
    MapDialogRect(window.window, rect);
}

void AfxWin2ControlInline_005e07c2(MfcCWndCompat& window, int id) {
    window.dialog_control_id = id;
}

void AfxWin2ControlInline_005e07d8(MfcCWndCompat& window) {
    CWndSendMessageInline(window, WM_NEXTDLGCTL, 0, 0);
}

void AfxWin2ControlInline_005e0827(MfcCWndCompat& window) {
    CWndSendMessageInline(window, WM_NEXTDLGCTL, 1, 0);
}

void AfxWin2ControlInline_005e0876(MfcCWndCompat& window,
    MfcCWndCompat& target) {
    CWndSendMessageInline(window, WM_NEXTDLGCTL,
        reinterpret_cast<WPARAM>(target.window), TRUE);
}

void AfxWin2ControlInline_005e08cc(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0401, value, 0);
}

void AfxWin2ControlInline_005e0922(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0400, 0, 0);
}

MfcStaticCompat& AfxWin2ControlInline_005e0974(MfcStaticCompat& control) {
    ConstructCWnd(control);
    control.runtime_class = GetStaticRuntimeClassCompat();
    control.style = 0;
    return control;
}

void AfxWin2ControlInline_005e0993(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0170, value, 0);
}

void AfxWin2ControlInline_005e09e9(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0171, 0, 0);
}

void AfxWin2ControlInline_005e0a3b(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0172, 3, value);
}

void AfxWin2ControlInline_005e0a91(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0173, 3, 0);
}

void AfxWin2ControlInline_005e0ae3(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0172, 0, value);
}

void AfxWin2ControlInline_005e0b39(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0173, 0, 0);
}

void AfxWin2ControlInline_005e0b8b(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0172, 2, value);
}

void AfxWin2ControlInline_005e0be1(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0173, 2, 0);
}

MfcButtonCompat& AfxWin2ControlInline_005e0c33(MfcButtonCompat& control) {
    ConstructCWnd(control);
    control.runtime_class = GetButtonRuntimeClassCompat();
    control.style = 0;
    return control;
}

void AfxWin2ControlInline_005e0c52(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00f2, 0, 0);
}

void AfxWin2ControlInline_005e0cc9(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00f0, 0, 0);
}

void AfxWin2ControlInline_005e0d1b(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00f1, value, 0);
}

UINT AfxWin2ControlInline_005e0d71(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x250)) {
            CrtDebugBreak();
        }
        return 0;
    }
    return static_cast<UINT>(GetWindowLongA(window.window, GWL_STYLE)) & 0xffU;
}

void AfxWin2ControlInline_005e0dc1(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x00f4, wparam, lparam);
}

void AfxWin2ControlInline_005e0e19(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00f7, 1, value);
}

void AfxWin2ControlInline_005e0e6f(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00f6, 1, 0);
}

void AfxWin2ControlInline_005e0ec1(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00f7, 0, value);
}

void AfxWin2ControlInline_005e0f17(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00f6, 0, 0);
}

void AfxWin2ControlInline_005e0f69(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00f7, 2, value);
}

void AfxWin2ControlInline_005e0fbf(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00f6, 2, 0);
}

MfcListBoxCompat& AfxWin2ControlInline_005e1011(
    MfcListBoxCompat& control) {
    ConstructCWnd(control);
    control.runtime_class = GetListBoxRuntimeClassCompat();
    control.style = 0;
    return control;
}

void AfxWin2ControlInline_005e1030(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x018b, 0, 0);
}

void AfxWin2ControlInline_005e1082(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0188, 0, 0);
}

void AfxWin2ControlInline_005e10d4(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0186, value, 0);
}

void AfxWin2ControlInline_005e112a(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0193, 0, 0);
}

void AfxWin2ControlInline_005e117c(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0194, value, 0);
}

void AfxWin2ControlInline_005e11d2(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 400, 0, 0);
}

void AfxWin2ControlInline_005e1224(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0191, wparam, lparam);
}

void AfxWin2ControlInline_005e127c(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x018e, 0, 0);
}

void AfxWin2ControlInline_005e12ce(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0197, value, 0);
}

void AfxWin2ControlInline_005e1324(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0199, value, 0);
}

void AfxWin2ControlInline_005e13d2(MfcCWndCompat& window, WPARAM value) {
    AfxWin2ControlInline_005e1324(window, value);
}

void AfxWin2ControlInline_005e137a(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x019a, wparam, lparam);
}

void AfxWin2ControlInline_005e1428(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x27f)) {
            CrtDebugBreak();
        }
        return;
    }
    AfxWin2ControlInline_005e137a(window, wparam, lparam);
}

void AfxWin2ControlInline_005e1476(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0198, wparam, lparam);
}

void AfxWin2ControlInline_005e14ce(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0187, value, 0);
}

void AfxWin2ControlInline_005e1524(MfcCWndCompat& window, LPARAM lparam,
    WPARAM wparam) {
    CWndSendMessageInline(window, 0x0185, wparam, lparam);
}

void AfxWin2ControlInline_005e157c(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0189, wparam, lparam);
}

void AfxWin2ControlInline_005e15d4(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x018a, value, 0);
}

void AfxWin2ControlInline_005e162a(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0195, value, 0);
}

void AfxWin2ControlInline_005e1680(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0192, wparam, lparam);
}

void AfxWin2ControlInline_005e16d8(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0192, 0, 0);
}

void AfxWin2ControlInline_005e1748(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0192, 1, value);
}

void AfxWin2ControlInline_005e179e(MfcCWndCompat& window, WPARAM wparam,
    UINT value) {
    CWndSendMessageInline(window, 0x01a0, wparam, value & 0xffffU);
}

void AfxWin2ControlInline_005e17fb(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x01a1, value, 0);
}

void AfxWin2ControlInline_005e1851(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x01a2, wparam, lparam);
}

void AfxWin2ControlInline_005e18a9(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x019f, 0, 0);
}

void AfxWin2ControlInline_005e18fb(MfcCWndCompat& window, WPARAM wparam,
    UINT value) {
    CWndSendMessageInline(window, 0x019e, wparam, value & 0xffffU);
}

void AfxWin2ControlInline_005e1958(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0180, 0, value);
}

void AfxWin2ControlInline_005e19ae(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0182, value, 0);
}

void AfxWin2ControlInline_005e1a04(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0181, wparam, lparam);
}

void AfxWin2ControlInline_005e1a5c(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0184, 0, 0);
}

void AfxWin2ControlInline_005e1aae(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x018d, wparam, lparam);
}

void AfxWin2ControlInline_005e1b06(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 399, wparam, lparam);
}

void AfxWin2ControlInline_005e1b5e(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x018c, wparam, lparam);
}

LRESULT AfxWin2ControlInline_005e1bb6(MfcCWndCompat& window, int swap,
    WPARAM first, WPARAM second) {
    return CWndSendMessageInline(window, 0x0183,
        swap == 0 ? second : first, swap == 0 ? first : second);
}

void AfxWin2ControlInline_005e1c3b(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x019c, value, 0);
}

void AfxWin2ControlInline_005e1c91(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x019d, 0, 0);
}

void AfxWin2ControlInline_005e1ce3(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x01a6, 0, 0);
}

void AfxWin2ControlInline_005e1d35(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x01a5, value, 0);
}

void AfxWin2ControlInline_005e1d8b(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x01a8, wparam, lparam);
}

MfcCheckListBoxCompat& AfxWin2ControlInline_005e1de3(
    MfcCheckListBoxCompat& control) {
    AfxWin2ControlInline_005e1011(control);
    control.check_style = 0;
    control.item_height = 0;
    return control;
}

int AfxWin2ControlInline_005e1e16(const MfcCheckListBoxCompat& control) {
    return control.item_height;
}

MfcComboBoxCompat& AfxWin2ControlInline_005e1e27(
    MfcComboBoxCompat& control) {
    ConstructCWnd(control);
    control.runtime_class = GetComboBoxRuntimeClassCompat();
    control.style = 0;
    return control;
}

void AfxWin2ControlInline_005e1e46(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0146, 0, 0);
}

void AfxWin2ControlInline_005e1e98(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0147, 0, 0);
}

void AfxWin2ControlInline_005e1eea(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x014e, value, 0);
}

void AfxWin2ControlInline_005e1f40(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0140, 0, 0);
}

void AfxWin2ControlInline_005e1f92(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0141, value, 0);
}

void AfxWin2ControlInline_005e1fe8(MfcCWndCompat& window, UINT low,
    int high) {
    CWndSendMessageInline(window, 0x0142, 0,
        (low & 0xffffU) | (static_cast<LPARAM>(high) << 16));
}

void AfxWin2ControlInline_005e2051(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0150, value, 0);
}

void AfxWin2ControlInline_005e20a7(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0151, wparam, lparam);
}

void AfxWin2ControlInline_005e20ff(MfcCWndCompat& window, WPARAM value) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x2d5)) {
            CrtDebugBreak();
        }
        return;
    }
    AfxWin2ControlInline_005e2051(window, value);
}

void AfxWin2ControlInline_005e2149(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x2d7)) {
            CrtDebugBreak();
        }
        return;
    }
    AfxWin2ControlInline_005e20a7(window, wparam, lparam);
}

void AfxWin2ControlInline_005e2197(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0148, wparam, lparam);
}

void AfxWin2ControlInline_005e21ef(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0149, value, 0);
}

void AfxWin2ControlInline_005e2245(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x014f, value, 0);
}

void AfxWin2ControlInline_005e229b(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0143, 0, value);
}

void AfxWin2ControlInline_005e22f1(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0144, value, 0);
}

void AfxWin2ControlInline_005e2347(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x014a, wparam, lparam);
}

void AfxWin2ControlInline_005e239f(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x014b, 0, 0);
}

void AfxWin2ControlInline_005e23f1(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0145, wparam, lparam);
}

void AfxWin2ControlInline_005e2449(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x014c, wparam, lparam);
}

void AfxWin2ControlInline_005e24a1(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x014d, wparam, lparam);
}

void AfxWin2ControlInline_005e24f9(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0303, 0, 0);
}

void AfxWin2ControlInline_005e254b(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0301, 0, 0);
}

void AfxWin2ControlInline_005e259d(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0300, 0, 0);
}

void AfxWin2ControlInline_005e25ef(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0302, 0, 0);
}

void AfxWin2ControlInline_005e2641(MfcCWndCompat& window, WPARAM wparam,
    UINT value) {
    CWndSendMessageInline(window, 0x0153, wparam, value & 0xffffU);
}

void AfxWin2ControlInline_005e269e(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0154, value, 0);
}

void AfxWin2ControlInline_005e26f4(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0158, wparam, lparam);
}

void AfxWin2ControlInline_005e274c(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0155, value, 0);
}

void AfxWin2ControlInline_005e27a2(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0156, 0, 0);
}

void AfxWin2ControlInline_005e27f4(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x0152, 0, value);
}

void AfxWin2ControlInline_005e284a(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0157, 0, 0);
}

void AfxWin2ControlInline_005e289c(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x015a, 0, 0);
}

void AfxWin2ControlInline_005e28ee(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0159, value, 0);
}

void AfxWin2ControlInline_005e2944(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x015b, 0, 0);
}

void AfxWin2ControlInline_005e2996(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x015c, value, 0);
}

void AfxWin2ControlInline_005e29ec(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x0161, wparam, lparam);
}

void AfxWin2ControlInline_005e2a44(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x015e, value, 0);
}

void AfxWin2ControlInline_005e2a9a(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x015d, 0, 0);
}

void AfxWin2ControlInline_005e2aec(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x0160, value, 0);
}

void AfxWin2ControlInline_005e2b42(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x015f, 0, 0);
}

MfcEditCompat& AfxWin2ControlInline_005e2b94(MfcEditCompat& control) {
    ConstructCWnd(control);
    control.runtime_class = GetEditRuntimeClassCompat();
    control.style = 0;
    return control;
}

void AfxWin2ControlInline_005e2bb3(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00c6, 0, 0);
}

void AfxWin2ControlInline_005e2c05(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00ba, 0, 0);
}

void AfxWin2ControlInline_005e2c57(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00b8, 0, 0);
}

void AfxWin2ControlInline_005e2ca9(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00b9, value, 0);
}

void AfxWin2ControlInline_005e2cff(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00b2, 0, value);
}

void AfxWin2ControlInline_005e2d55(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x00b0, wparam, lparam);
}

void AfxWin2ControlInline_005e2dad(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00b0, 0, 0);
}

void AfxWin2ControlInline_005e2dff(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00bd, 0, 0);
}

void AfxWin2ControlInline_005e2e51(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00bc, value, 0);
}

void AfxWin2ControlInline_005e2ea7(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x00c4, wparam, lparam);
}

void AfxWin2ControlInline_005e2eff(MfcCWndCompat& window, WPARAM wparam,
    WORD* buffer, WORD value) {
    if (buffer != nullptr) {
        *buffer = value;
    }
    CWndSendMessageInline(window, 0x00c4, wparam,
        reinterpret_cast<LPARAM>(buffer));
}

void AfxWin2ControlInline_005e2f61(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00cd, 0, 0);
}

void AfxWin2ControlInline_005e2fb3(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 200, value, 0);
}

void AfxWin2ControlInline_005e3009(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00c5, value, 0);
}

void AfxWin2ControlInline_005e305f(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00c9, value, 0);
}

void AfxWin2ControlInline_005e30b5(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00bb, value, 0);
}

void AfxWin2ControlInline_005e310b(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00c1, value, 0);
}

void AfxWin2ControlInline_005e3161(MfcCWndCompat& window, LPARAM lparam,
    WPARAM wparam) {
    CWndSendMessageInline(window, 0x00b6, wparam, lparam);
}

void AfxWin2ControlInline_005e31b9(MfcCWndCompat& window, LPARAM lparam,
    WPARAM wparam) {
    CWndSendMessageInline(window, 0x00c2, wparam, lparam);
}

void AfxWin2ControlInline_005e3211(MfcCWndCompat& window, char value) {
    CWndSendMessageInline(window, 0x00cc, static_cast<int>(value), 0);
}

void AfxWin2ControlInline_005e3268(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00b3, 0, value);
}

void AfxWin2ControlInline_005e32be(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00b4, 0, value);
}

void AfxWin2ControlInline_005e3314(MfcCWndCompat& window, UINT range,
    int skip_scroll_caret) {
    CWndSendMessageInline(window, 0x00b1, range & 0xffffU, range >> 16);
    if (skip_scroll_caret == 0) {
        CWndSendMessageInline(window, 0x00b7, 0, 0);
    }
}

void AfxWin2ControlInline_005e339b(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam, int skip_scroll_caret) {
    CWndSendMessageInline(window, 0x00b1, wparam, lparam);
    if (skip_scroll_caret == 0) {
        CWndSendMessageInline(window, 0x00b7, 0, 0);
    }
}

void AfxWin2ControlInline_005e340f(MfcCWndCompat& window, WPARAM wparam,
    LPARAM lparam) {
    CWndSendMessageInline(window, 0x00cb, wparam, lparam);
}

void AfxWin2ControlInline_005e3467(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00cb, 0, 0);
}

void AfxWin2ControlInline_005e34d7(MfcCWndCompat& window, LPARAM value) {
    CWndSendMessageInline(window, 0x00cb, 1, value);
}

void AfxWin2ControlInline_005e352d(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 199, 0, 0);
}

void AfxWin2ControlInline_005e357f(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0303, 0, 0);
}

void AfxWin2ControlInline_005e35d1(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0301, 0, 0);
}

void AfxWin2ControlInline_005e3623(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0300, 0, 0);
}

void AfxWin2ControlInline_005e3675(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x0302, 0, 0);
}

void AfxWin2ControlInline_005e36c7(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00cf, value, 0);
}

void AfxWin2ControlInline_005e371d(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00ce, 0, 0);
}

void AfxWin2ControlInline_005e376f(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00d2, 0, 0);
}

void AfxWin2ControlInline_005e37c1(MfcCWndCompat& window, UINT low,
    int high) {
    CWndSendMessageInline(window, 0x00d3, 3,
        (low & 0xffffU) | (static_cast<LPARAM>(high) << 16));
}

void AfxWin2ControlInline_005e382a(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00d4, 0, 0);
}

void AfxWin2ControlInline_005e387c(MfcCWndCompat& window, WPARAM value) {
    CWndSendMessageInline(window, 0x00c5, value, 0);
}

void AfxWin2ControlInline_005e38d2(MfcCWndCompat& window) {
    CWndSendMessageInline(window, 0x00d5, 0, 0);
}

POINT AfxWin2ControlInline_005e3924(MfcCWndCompat& window, WPARAM value) {
    return PointConstructFromDWord(
        static_cast<DWORD>(CWndSendMessageInline(window, 0x00d6, value, 0)));
}

void AfxWin2ControlInline_005e3986(MfcCWndCompat& window, UINT low,
    int high) {
    CWndSendMessageInline(window, 0x00d7, 0,
        (low & 0xffffU) | (static_cast<LPARAM>(high) << 16));
}

MfcScrollBarCompat& AfxWin2ControlInline_005e39ef(
    MfcScrollBarCompat& control) {
    ConstructCWnd(control);
    control.runtime_class = GetScrollBarRuntimeClassCompat();
    control.style = 0;
    return control;
}

void AfxWin2ControlInline_005e3a0e(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x37d)) {
            CrtDebugBreak();
        }
        return;
    }
    GetScrollPos(window.window, SB_CTL);
}

BOOL AfxWin2ControlInline_005e3bfe(MfcCWndCompat& window,
    SCROLLINFO& scroll_info, BOOL redraw) {
    return CWndSetScrollInfoCompat(window, SB_CTL, scroll_info, redraw);
}

BOOL AfxWin2ControlInline_005e3c1d(MfcCWndCompat& window,
    SCROLLINFO& scroll_info, UINT mask) {
    return CWndGetScrollInfoCompat(window, SB_CTL, scroll_info, mask);
}

int AfxWin2ControlInline_005e3c3c(MfcCWndCompat& window) {
    return CWndGetScrollLimitCompat(window, SB_CTL);
}

HWND AfxMdiClientWindow(MfcFrameWndCompat& frame) {
    if (frame.mdi_client != nullptr && IsWindow(frame.mdi_client)) {
        return frame.mdi_client;
    }
    frame.mdi_client = frame.window == nullptr ? nullptr
        : FindWindowExA(frame.window, nullptr, "MDICLIENT", nullptr);
    return frame.mdi_client;
}

void AfxWin2ControlInline_005e3c51(MfcFrameWndCompat& frame,
    MfcCWndCompat& target) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDIACTIVATE,
        reinterpret_cast<WPARAM>(target.window), 0);
}

void AfxWin2ControlInline_005e3cad(MfcFrameWndCompat& frame) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDIICONARRANGE, 0, 0);
}

void AfxWin2ControlInline_005e3d02(MfcFrameWndCompat& frame,
    MfcCWndCompat& target) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDIMAXIMIZE,
        reinterpret_cast<WPARAM>(target.window), 0);
}

void AfxWin2ControlInline_005e3d5e(MfcFrameWndCompat& frame) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDINEXT, 0, 0);
}

void AfxWin2ControlInline_005e3db3(MfcFrameWndCompat& frame,
    MfcCWndCompat& target) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDIRESTORE,
        reinterpret_cast<WPARAM>(target.window), 0);
}

MfcMenuCompat* AfxWin2ControlInline_005e3e0f(MfcFrameWndCompat& frame,
    MfcMenuCompat* frame_menu, MfcMenuCompat* window_menu) {
    HMENU previous = reinterpret_cast<HMENU>(SendMessageA(
        AfxMdiClientWindow(frame), WM_MDISETMENU,
        reinterpret_cast<WPARAM>(MenuGetSafeHandle(frame_menu)),
        reinterpret_cast<LPARAM>(MenuGetSafeHandle(window_menu))));
    return CMenuFromHandle(previous);
}

void AfxWin2ControlInline_005e3e7a(MfcFrameWndCompat& frame) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDITILE, 0, 0);
}

void AfxWin2ControlInline_005e3ecf(MfcFrameWndCompat& frame) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDICASCADE, 0, 0);
}

void AfxWin2ControlInline_005e3f24(MfcFrameWndCompat& frame, WPARAM value) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDICASCADE, value, 0);
}

void AfxWin2ControlInline_005e3f7d(MfcFrameWndCompat& frame, WPARAM value) {
    SendMessageA(AfxMdiClientWindow(frame), WM_MDITILE, value, 0);
}

void AfxWin2ControlInline_005e3fd6(MfcCWndCompat& window) {
    if (window.window != nullptr) {
        SendMessageA(GetParent(window.window), 0x0221,
            reinterpret_cast<WPARAM>(window.window), 0);
    }
}

void AfxWin2ControlInline_005e4032(MfcCWndCompat& window) {
    if (window.window != nullptr) {
        SendMessageA(GetParent(window.window), 0x0222,
            reinterpret_cast<WPARAM>(window.window), 0);
    }
}

void AfxWin2ControlInline_005e408e(MfcCWndCompat& window) {
    if (window.window != nullptr) {
        SendMessageA(GetParent(window.window), 0x0225,
            reinterpret_cast<WPARAM>(window.window), 0);
    }
}

void AfxWin2ControlInline_005e40ea(MfcCWndCompat& window) {
    if (window.window != nullptr) {
        SendMessageA(GetParent(window.window), 0x0223,
            reinterpret_cast<WPARAM>(window.window), 0);
    }
}

MfcDocumentCompat* AfxWin2ControlInline_005e4146(MfcViewCompat* view) {
    if (view == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3b4)) {
            CrtDebugBreak();
        }
        return nullptr;
    }
    return static_cast<MfcDocumentCompat*>(view->document);
}

SIZE AfxWin2ControlInline_005e417d(MfcScrollViewCompat* view) {
    if (view == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3b6)) {
            CrtDebugBreak();
        }
        return {};
    }
    return view->total_log;
}

const std::string& AfxWin2ControlInline_005e41c4(
    const MfcDocumentCompat* document) {
    static const std::string empty_title;
    if (document == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3ba)) {
            CrtDebugBreak();
        }
        return empty_title;
    }
    return document->title;
}

const std::string& AfxWin2ControlInline_005e41fb(
    const MfcDocumentCompat* document) {
    static const std::string empty_path_name;
    if (document == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3bc)) {
            CrtDebugBreak();
        }
        return empty_path_name;
    }
    return document->path_name;
}

MfcDocTemplateCompat* AfxWin2ControlInline_005e4232(
    MfcDocumentCompat* document) {
    if (document == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3be)) {
            CrtDebugBreak();
        }
        return nullptr;
    }
    return document->doc_template;
}

BOOL AfxWin2ControlInline_005e4269(const MfcDocumentCompat* document) {
    if (document == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3c0)) {
            CrtDebugBreak();
        }
        return FALSE;
    }
    return document->modified ? TRUE : FALSE;
}

void AfxWin2ControlInline_005e42a0(MfcDocumentCompat* document,
    BOOL modified) {
    if (document == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3c2)) {
            CrtDebugBreak();
        }
        return;
    }
    document->modified = modified != FALSE;
}

BOOL AfxWin2ControlInline_005e4304(MfcWinThreadCompat& thread,
    int priority) {
    if (thread.thread == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3c8)) {
            CrtDebugBreak();
        }
        return FALSE;
    }
    return SetThreadPriority(thread.thread, priority);
}

int AfxWin2ControlInline_005e434b(MfcWinThreadCompat& thread) {
    if (thread.thread == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3ca)) {
            CrtDebugBreak();
        }
        return THREAD_PRIORITY_ERROR_RETURN;
    }
    return GetThreadPriority(thread.thread);
}

DWORD AfxWin2ControlInline_005e438c(MfcWinThreadCompat& thread) {
    if (thread.thread == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3cc)) {
            CrtDebugBreak();
        }
        return static_cast<DWORD>(-1);
    }
    return ResumeThread(thread.thread);
}

DWORD AfxWin2ControlInline_005e43cd(MfcWinThreadCompat& thread) {
    if (thread.thread == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3ce)) {
            CrtDebugBreak();
        }
        return static_cast<DWORD>(-1);
    }
    return SuspendThread(thread.thread);
}

BOOL AfxWin2ControlInline_005e440e(MfcWinThreadCompat& thread,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (thread.thread == nullptr) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3d0)) {
            CrtDebugBreak();
        }
        return FALSE;
    }
    return PostThreadMessageA(thread.thread_id, message, wparam, lparam);
}

HCURSOR AfxWin2ControlInline_005e445d(MfcWinAppCompat& app, LPCSTR name) {
    (void)app;
    return LoadCursorA(AfxGetResourceHandleCompat(), name);
}

HCURSOR AfxWin2ControlInline_005e447a(MfcWinAppCompat& app, UINT resource_id) {
    (void)app;
    return LoadCursorA(AfxGetResourceHandleCompat(),
        MAKEINTRESOURCEA(resource_id & 0xffffU));
}

HCURSOR AfxWin2ControlInline_005e449c(LPCSTR name) {
    return LoadCursorA(nullptr, name);
}

HCURSOR AfxWin2ControlInline_005e44b5(UINT resource_id) {
    return LoadCursorA(nullptr, MAKEINTRESOURCEA(resource_id & 0xffffU));
}

HICON AfxWin2ControlInline_005e44d3(MfcWinAppCompat& app, LPCSTR name) {
    (void)app;
    return LoadIconA(AfxGetResourceHandleCompat(), name);
}

HICON AfxWin2ControlInline_005e44f0(MfcWinAppCompat& app, UINT resource_id) {
    (void)app;
    return LoadIconA(AfxGetResourceHandleCompat(),
        MAKEINTRESOURCEA(resource_id & 0xffffU));
}

HICON AfxWin2ControlInline_005e4512(LPCSTR name) {
    return LoadIconA(nullptr, name);
}

HICON AfxWin2ControlInline_005e452b(UINT resource_id) {
    return LoadIconA(nullptr, MAKEINTRESOURCEA(resource_id & 0xffffU));
}

void AfxWin2ControlInline_005e4563() {
    AfxEndWaitCursor();
}

void AfxWin2ControlInline_005e457a() {
    AfxRestoreWaitCursor();
}

BOOL AfxWin2ControlInline_005e4591(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3f3)) {
            CrtDebugBreak();
        }
        return FALSE;
    }
    return CloseWindow(window.window);
}

BOOL AfxWin2ControlInline_005e45da(MfcCWndCompat& window) {
    if (window.window == nullptr || !IsWindow(window.window)) {
        if (AfxAssertFailedLine("afxwin2.inl", 0x3f5)) {
            CrtDebugBreak();
        }
        return FALSE;
    }
    return OpenIcon(window.window);
}

MfcStaticCompat* AfxWin2ControlInline_005e4630(MfcStaticCompat* control,
    unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    DestroyStaticControl(*control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

MfcButtonCompat* AfxWin2ControlInline_005e4660(MfcButtonCompat* control,
    unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    DestroyButtonControl(*control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

MfcListBoxCompat* AfxWin2ControlInline_005e4690(MfcListBoxCompat* control,
    unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    DestroyListBoxControl(*control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

void AfxWin2ControlInline_005e46f0(MfcListBoxCompat* control);

MfcListBoxCompat* AfxWin2ControlInline_005e46c0(MfcListBoxCompat* control,
    unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    AfxWin2ControlInline_005e46f0(control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

void AfxWin2ControlInline_005e46f0(MfcListBoxCompat* control) {
    if (control != nullptr) {
        DestroyListBoxControl(*control);
    }
}

MfcComboBoxCompat* AfxWin2ControlInline_005e4710(MfcComboBoxCompat* control,
    unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    DestroyComboBoxControl(*control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

MfcEditCompat* AfxWin2ControlInline_005e4740(MfcEditCompat* control,
    unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    DestroyEditControl(*control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

MfcScrollBarCompat* AfxWin2ControlInline_005e4770(
    MfcScrollBarCompat* control, unsigned flags) {
    if (control == nullptr) {
        return nullptr;
    }
    DestroyScrollBarControl(*control);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(control);
    }
    return control;
}

BOOL CWndEqualsInline(const MfcCWndCompat& left,
    const MfcCWndCompat& right) {
    return left.window == right.window;
}

HWND CWndGetSafeHwndInline(const MfcCWndCompat* window) {
    return window == nullptr ? nullptr : window->window;
}

BOOL CWndUpdateWindowInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : UpdateWindow(window.window);
}

void CWndSetRedrawInline(MfcCWndCompat& window, BOOL redraw) {
    if (window.window != nullptr) {
        SendMessageA(window.window, WM_SETREDRAW,
            static_cast<WPARAM>(redraw), 0);
    }
}

BOOL CWndLockWindowUpdateInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : LockWindowUpdate(window.window);
}

BOOL CWndUnlockWindowUpdateInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : LockWindowUpdate(nullptr);
}

MfcCWndCompat* CWndFindWindowInline(const char* class_name,
    const char* window_name) {
    HWND found = FindWindowA(class_name, window_name);
    return found == nullptr ? nullptr : CWndFromHandle(found);
}

MfcCWndCompat* CWndWindowFromPointInline(LONG x, LONG y) {
    POINT point{x, y};
    HWND window = WindowFromPoint(point);
    return window == nullptr ? nullptr : CWndFromHandle(window);
}

BOOL CWndDrawMenuBarInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : DrawMenuBar(window.window);
}

BOOL CWndIsIconicInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : IsIconic(window.window);
}

BOOL CWndIsZoomedInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : IsZoomed(window.window);
}

UINT CWndArrangeIconicWindowsInline(MfcCWndCompat& window) {
    return window.window == nullptr ? 0 : ArrangeIconicWindows(window.window);
}

int CWndSetWindowRgnInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* region, BOOL redraw) {
    return window.window == nullptr ? 0
        : SetWindowRgn(window.window,
            static_cast<HRGN>(GdiObjectGetSafeHandle(region)), redraw);
}

BOOL CWndBringWindowToTopInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : BringWindowToTop(window.window);
}

BOOL CWndGetWindowRectInline(MfcCWndCompat& window, RECT& rect) {
    return window.window == nullptr ? FALSE : GetWindowRect(window.window, &rect);
}

BOOL CWndGetClientRectInline(MfcCWndCompat& window, RECT& rect) {
    return window.window == nullptr ? FALSE : GetClientRect(window.window, &rect);
}

BOOL CWndClientToScreenInline(MfcCWndCompat& window, POINT& point) {
    return window.window == nullptr ? FALSE
        : ClientToScreen(window.window, &point);
}

BOOL CWndScreenToClientInline(MfcCWndCompat& window, POINT& point) {
    return window.window == nullptr ? FALSE
        : ScreenToClient(window.window, &point);
}

LRESULT CWndSendMessageInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return window.window == nullptr ? 0
        : SendMessageA(window.window, message, wparam, lparam);
}

BOOL CWndPostMessageInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return window.window == nullptr ? FALSE
        : PostMessageA(window.window, message, wparam, lparam);
}

void CWndSetFontInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* font, BOOL redraw) {
    if (window.window != nullptr) {
        SendMessageA(window.window, WM_SETFONT,
            reinterpret_cast<WPARAM>(FontGetSafeHandle(font)), redraw);
    }
}

MfcGdiObjectCompat* CWndGetFontInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    return FontFromHandle(reinterpret_cast<HFONT>(
        SendMessageA(window.window, WM_GETFONT, 0, 0)));
}

MfcMenuCompat* CWndGetMenuInline(MfcCWndCompat& window) {
    return window.window == nullptr ? nullptr : CMenuFromHandle(GetMenu(window.window));
}

BOOL CWndSetMenuInline(MfcCWndCompat& window, const MfcMenuCompat* menu) {
    return window.window == nullptr ? FALSE
        : SetMenu(window.window, MenuGetSafeHandle(menu));
}

MfcMenuCompat* CWndGetSystemMenuInline(MfcCWndCompat& window, BOOL revert) {
    return window.window == nullptr ? nullptr
        : CMenuFromHandle(GetSystemMenu(window.window, revert));
}

BOOL CWndHiliteMenuItemInline(MfcCWndCompat& window,
    const MfcMenuCompat& menu, UINT item, UINT flags) {
    return window.window == nullptr ? FALSE
        : HiliteMenuItem(window.window, MenuGetSafeHandle(&menu), item, flags);
}

int CWndGetWindowRgnInline(MfcCWndCompat& window, MfcGdiObjectCompat& region) {
    return window.window == nullptr ? ERROR
        : GetWindowRgn(window.window,
            static_cast<HRGN>(GdiObjectGetSafeHandle(&region)));
}

int CWndMapWindowPointsInline(MfcCWndCompat& window, MfcCWndCompat* to,
    POINT* points, UINT count) {
    return window.window == nullptr ? 0
        : MapWindowPoints(window.window, to == nullptr ? nullptr : to->window,
            points, count);
}

int CWndMapWindowRectInline(MfcCWndCompat& window, MfcCWndCompat* to,
    RECT& rect) {
    return CWndMapWindowPointsInline(window, to,
        reinterpret_cast<POINT*>(&rect), 2);
}

HDC CWndBeginPaintInline(MfcCWndCompat& window, PAINTSTRUCT& paint) {
    return window.window == nullptr ? nullptr : BeginPaint(window.window, &paint);
}

BOOL CWndEndPaintInline(MfcCWndCompat& window, const PAINTSTRUCT& paint) {
    return window.window == nullptr ? FALSE : EndPaint(window.window, &paint);
}

HDC CWndGetDCInline(MfcCWndCompat& window) {
    return window.window == nullptr ? nullptr : GetDC(window.window);
}

HDC CWndGetWindowDCInline(MfcCWndCompat& window) {
    return window.window == nullptr ? nullptr : GetWindowDC(window.window);
}

int CWndReleaseDCInline(MfcCWndCompat& window, HDC dc) {
    return window.window == nullptr ? 0 : ReleaseDC(window.window, dc);
}

int CWndGetUpdateRgnInline(MfcCWndCompat& window, MfcGdiObjectCompat& region,
    BOOL erase) {
    return window.window == nullptr ? ERROR
        : GetUpdateRgn(window.window,
            static_cast<HRGN>(GdiObjectGetSafeHandle(&region)), erase);
}

BOOL CWndGetUpdateRectInline(MfcCWndCompat& window, RECT* rect, BOOL erase) {
    return window.window == nullptr ? FALSE : GetUpdateRect(window.window, rect,
        erase);
}

BOOL CWndInvalidateInline(MfcCWndCompat& window, BOOL erase) {
    return CWndInvalidateRectInline(window, nullptr, erase);
}

BOOL CWndInvalidateRectInline(MfcCWndCompat& window, const RECT* rect,
    BOOL erase) {
    return window.window == nullptr ? FALSE : InvalidateRect(window.window,
        rect, erase);
}

BOOL CWndInvalidateRgnInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* region, BOOL erase) {
    return window.window == nullptr ? FALSE
        : InvalidateRgn(window.window,
            static_cast<HRGN>(GdiObjectGetSafeHandle(region)), erase);
}

BOOL CWndValidateRgnInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* region) {
    return window.window == nullptr ? FALSE
        : ValidateRgn(window.window,
            static_cast<HRGN>(GdiObjectGetSafeHandle(region)));
}

BOOL CWndValidateRectInline(MfcCWndCompat& window, const RECT* rect) {
    return window.window == nullptr ? FALSE : ValidateRect(window.window, rect);
}

BOOL CWndIsWindowVisibleInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : IsWindowVisible(window.window);
}

BOOL CWndShowOwnedPopupsInline(MfcCWndCompat& window, BOOL show) {
    return window.window == nullptr ? FALSE : ShowOwnedPopups(window.window, show);
}

void CWndSendMessageToDescendantsInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    send_message_to_descendants(window.window, message, wparam, lparam);
}

MfcCWndCompat* CWndGetDescendantWindowInline(MfcCWndCompat& window,
    int control_id, bool only_permanent) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND child = GetDlgItem(window.window, control_id);
    if (child == nullptr) {
        return nullptr;
    }
    return only_permanent ? CWndFromHandlePermanent(child) : CWndFromHandle(child);
}

HDC CWndGetDCExInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* clip_region, DWORD flags) {
    return window.window == nullptr ? nullptr
        : GetDCEx(window.window,
            static_cast<HRGN>(GdiObjectGetSafeHandle(clip_region)), flags);
}

BOOL CWndRedrawWindowInline(MfcCWndCompat& window, const RECT* update_rect,
    const MfcGdiObjectCompat* update_region, UINT flags) {
    return window.window == nullptr ? FALSE
        : RedrawWindow(window.window, update_rect,
            static_cast<HRGN>(GdiObjectGetSafeHandle(update_region)), flags);
}

BOOL CWndEnableScrollBarInline(MfcCWndCompat& window, int bar, UINT arrows) {
    return window.window == nullptr ? FALSE
        : EnableScrollBar(window.window, bar, arrows);
}

UINT_PTR CWndSetTimerInline(MfcCWndCompat& window, UINT_PTR id_event,
    UINT elapsed, TIMERPROC proc) {
    return window.window == nullptr ? 0 : SetTimer(window.window, id_event,
        elapsed, proc);
}

int CWndDlgDirListInline(MfcCWndCompat& window, char* path_spec,
    int list_box_id, int static_path_id, UINT file_type) {
    return window.window == nullptr ? 0
        : DlgDirListA(window.window, path_spec, list_box_id, static_path_id,
            file_type);
}

int CWndDlgDirListComboBoxInline(MfcCWndCompat& window, char* path_spec,
    int combo_box_id, int static_path_id, UINT file_type) {
    return window.window == nullptr ? 0
        : DlgDirListComboBoxA(window.window, path_spec, combo_box_id,
            static_path_id, file_type);
}

BOOL CWndDlgDirSelectInline(MfcCWndCompat& window, char* output,
    int output_count, int list_box_id) {
    return window.window == nullptr ? FALSE
        : DlgDirSelectExA(window.window, output, output_count, list_box_id);
}

BOOL CWndDlgDirSelectComboBoxInline(MfcCWndCompat& window, char* output,
    int output_count, int combo_box_id) {
    return window.window == nullptr ? FALSE
        : DlgDirSelectComboBoxExA(window.window, output, output_count,
            combo_box_id);
}

MfcCWndCompat* CWndGetNextDlgGroupItemInline(MfcCWndCompat& window,
    MfcCWndCompat* control, BOOL previous) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND next = GetNextDlgGroupItem(window.window,
        control == nullptr ? nullptr : control->window, previous);
    return next == nullptr ? nullptr : CWndFromHandle(next);
}

MfcCWndCompat* CWndGetNextDlgTabItemInline(MfcCWndCompat& window,
    MfcCWndCompat* control, BOOL previous) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND next = GetNextDlgTabItem(window.window,
        control == nullptr ? nullptr : control->window, previous);
    return next == nullptr ? nullptr : CWndFromHandle(next);
}

BOOL CWndShowScrollBarInline(MfcCWndCompat& window, int bar, BOOL show) {
    return window.window == nullptr ? FALSE : ShowScrollBar(window.window, bar,
        show);
}

BOOL CWndKillTimerInline(MfcCWndCompat& window, UINT_PTR id_event) {
    return window.window == nullptr ? FALSE : KillTimer(window.window, id_event);
}

MfcCWndCompat* CWndSetActiveWindowInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND previous = SetActiveWindow(window.window);
    return previous == nullptr ? nullptr : CWndFromHandle(previous);
}

MfcCWndCompat* CWndSetCaptureInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND previous = SetCapture(window.window);
    return previous == nullptr ? nullptr : CWndFromHandle(previous);
}

MfcCWndCompat* CWndChildWindowFromPointInline(MfcCWndCompat& window,
    POINT point) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND child = ChildWindowFromPoint(window.window, point);
    return child == nullptr ? nullptr : CWndFromHandle(child);
}

MfcCWndCompat* CWndChildWindowFromPointExInline(MfcCWndCompat& window,
    POINT point, UINT flags) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND child = ChildWindowFromPointEx(window.window, point, flags);
    return child == nullptr ? nullptr : CWndFromHandle(child);
}

MfcCWndCompat* CWndGetNextWindowInline(MfcCWndCompat& window, UINT flags) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND next = GetWindow(window.window, flags);
    return next == nullptr ? nullptr : CWndFromHandle(next);
}

MfcCWndCompat* CWndGetWindowInline(MfcCWndCompat& window, UINT flags) {
    return CWndGetNextWindowInline(window, flags);
}

MfcCWndCompat* CWndGetTopWindowInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND child = GetTopWindow(window.window);
    return child == nullptr ? nullptr : CWndFromHandle(child);
}

MfcCWndCompat* CWndGetLastActivePopupInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND popup = GetLastActivePopup(window.window);
    return popup == nullptr ? nullptr : CWndFromHandle(popup);
}

MfcCWndCompat* CWndGetParentInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND parent = GetParent(window.window);
    return parent == nullptr ? nullptr : CWndFromHandle(parent);
}

BOOL CWndFlashWindowInline(MfcCWndCompat& window, BOOL invert) {
    return window.window == nullptr ? FALSE : FlashWindow(window.window, invert);
}

BOOL CWndChangeClipboardChainInline(MfcCWndCompat& window,
    MfcCWndCompat* next_window) {
    return window.window == nullptr ? FALSE
        : ChangeClipboardChain(window.window,
            next_window == nullptr ? nullptr : next_window->window);
}

MfcCWndCompat* CWndSetClipboardViewerInline(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND previous = SetClipboardViewer(window.window);
    return previous == nullptr ? nullptr : CWndFromHandle(previous);
}

MfcCWndCompat* CWndGetOpenClipboardWindowInline() {
    HWND window = GetOpenClipboardWindow();
    return window == nullptr ? nullptr : CWndFromHandle(window);
}

MfcCWndCompat* CWndGetClipboardOwnerInline() {
    HWND window = GetClipboardOwner();
    return window == nullptr ? nullptr : CWndFromHandle(window);
}

MfcCWndCompat* CWndGetClipboardViewerInline() {
    HWND window = GetClipboardViewer();
    return window == nullptr ? nullptr : CWndFromHandle(window);
}

BOOL CWndOpenClipboardInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : OpenClipboard(window.window);
}

BOOL CWndHideCaretInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : HideCaret(window.window);
}

BOOL CWndShowCaretInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : ShowCaret(window.window);
}

BOOL CWndSetForegroundWindowInline(MfcCWndCompat& window) {
    return window.window == nullptr ? FALSE : SetForegroundWindow(window.window);
}

BOOL CWndIsChildInline(MfcCWndCompat& window, const MfcCWndCompat* child) {
    return window.window == nullptr || child == nullptr ? FALSE
        : IsChild(window.window, child->window);
}

MfcCWndCompat* CWndSetParentInline(MfcCWndCompat& window,
    MfcCWndCompat* parent) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND old_parent = SetParent(window.window,
        parent == nullptr ? nullptr : parent->window);
    return old_parent == nullptr ? nullptr : CWndFromHandle(old_parent);
}

BOOL CWndSetCaretPosInline(int x, int y) {
    return SetCaretPos(x, y);
}

BOOL CWndSendNotifyMessageInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return window.window == nullptr ? FALSE
        : SendNotifyMessageA(window.window, message, wparam, lparam);
}

HICON CWndSetIconInline(MfcCWndCompat& window, HICON icon, BOOL big_icon) {
    return window.window == nullptr ? nullptr : reinterpret_cast<HICON>(
        SendMessageA(window.window, WM_SETICON,
            static_cast<WPARAM>(big_icon), reinterpret_cast<LPARAM>(icon)));
}

HICON CWndGetIconInline(MfcCWndCompat& window, BOOL big_icon) {
    return window.window == nullptr ? nullptr : reinterpret_cast<HICON>(
        SendMessageA(window.window, WM_GETICON,
            static_cast<WPARAM>(big_icon), 0));
}

void CWndPrintInline(MfcCWndCompat& window, const MfcCDCCompat* dc,
    DWORD flags) {
    if (window.window != nullptr) {
        SendMessageA(window.window, WM_PRINT,
            reinterpret_cast<WPARAM>(CDCGetSafeHdc(dc)), flags);
    }
}

void CWndPrintClientInline(MfcCWndCompat& window, const MfcCDCCompat* dc,
    DWORD flags) {
    if (window.window != nullptr) {
        SendMessageA(window.window, WM_PRINTCLIENT,
            reinterpret_cast<WPARAM>(CDCGetSafeHdc(dc)), flags);
    }
}

BOOL CWndSetContextHelpIdInline(MfcCWndCompat& window, DWORD context_id) {
    return window.window == nullptr ? FALSE
        : SetWindowContextHelpId(window.window, context_id);
}

DWORD CWndGetContextHelpIdInline(MfcCWndCompat& window) {
    return window.window == nullptr ? 0 : GetWindowContextHelpId(window.window);
}

BOOL CWndCreateBitmapCaretInline(MfcCWndCompat& window, HBITMAP bitmap) {
    return window.window == nullptr ? FALSE : CreateCaret(window.window, bitmap,
        0, 0);
}

BOOL CWndCreateSolidCaretInline(MfcCWndCompat& window, int width, int height) {
    return window.window == nullptr ? FALSE : CreateCaret(window.window,
        nullptr, width, height);
}

BOOL CWndCreateGrayCaretInline(MfcCWndCompat& window, int width, int height) {
    return window.window == nullptr ? FALSE : CreateCaret(window.window,
        reinterpret_cast<HBITMAP>(1), width, height);
}

int AfxWin2ControlInline_005e3a59(MfcCWndCompat& window, int position,
    BOOL redraw) {
    return window.window == nullptr ? 0 : SetScrollPos(window.window, SB_CTL,
        position, redraw);
}

BOOL AfxWin2ControlInline_005e3aae(MfcCWndCompat& window, int* min_position,
    int* max_position) {
    return window.window == nullptr ? FALSE : GetScrollRange(window.window,
        SB_CTL, min_position, max_position);
}

BOOL AfxWin2ControlInline_005e3b03(MfcCWndCompat& window, int min_position,
    int max_position, BOOL redraw) {
    return window.window == nullptr ? FALSE : SetScrollRange(window.window,
        SB_CTL, min_position, max_position, redraw);
}

BOOL AfxWin2ControlInline_005e3b5c(MfcCWndCompat& window, BOOL show) {
    return window.window == nullptr ? FALSE : ShowScrollBar(window.window,
        SB_CTL, show);
}

BOOL AfxWin2ControlInline_005e3bad(MfcCWndCompat& window, UINT arrows) {
    return window.window == nullptr ? FALSE : EnableScrollBar(window.window,
        SB_CTL, arrows);
}

void CWndInvokeHelper(MfcCWndCompat& window, LONG dispatch_id, WORD flags,
    unsigned short return_type, void* return_value,
    const unsigned char* param_info, ...) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    if (site == nullptr || site->invoke_helper == nullptr) {
        return;
    }
    va_list args;
    va_start(args, param_info);
    site->invoke_helper(*site, dispatch_id, flags, return_type, return_value,
        param_info, args);
    va_end(args);
}

void CWndGetProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, void* value) {
    CWndInvokeHelper(window, dispatch_id, 2, value_type, value, nullptr);
}

void CWndSetProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, ...) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    if (site == nullptr || site->set_property == nullptr) {
        return;
    }
    va_list args;
    va_start(args, value_type);
    site->set_property(*site, dispatch_id, value_type, args);
    va_end(args);
}

IUnknown* CWndGetControlUnknown(MfcCWndCompat& window) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    if (site == nullptr || site->control_unknown == nullptr) {
        return nullptr;
    }
    site->control_unknown->AddRef();
    return site->control_unknown;
}

bool CWndGetAmbientProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, void* value) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    if (site == nullptr || site->get_ambient_property == nullptr) {
        return false;
    }
    return site->get_ambient_property(*site, dispatch_id, value_type, value);
}

void CWndSetControlSize(MfcCWndCompat& window, int width, int height) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    if (site != nullptr && site->set_control_size != nullptr) {
        site->set_control_size(*site, width, height);
        return;
    }
    if (window.window != nullptr && IsWindow(window.window)) {
        SetWindowPos(window.window, nullptr, 0, 0, width, height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CWndAttachControlSiteFromParent(MfcCWndCompat* window) {
    if (window == nullptr || window->control_site != nullptr ||
        window->window == nullptr) {
        return;
    }
    HWND parent_handle = GetParent(window->window);
    if (parent_handle == nullptr) {
        return;
    }
    MfcCWndCompat* parent = CWndFromHandlePermanent(parent_handle);
    if (parent == nullptr) {
        parent = CWndFromHandle(parent_handle);
    }
    if (parent != nullptr) {
        CWndAttachControlSiteToParent(*window, *parent);
    }
}

void CWndAttachControlSiteToParent(MfcCWndCompat& window,
    MfcCWndCompat& parent) {
    if (window.control_site != nullptr || parent.control_container == nullptr) {
        return;
    }
    auto* container =
        static_cast<MfcOleControlContainerCompat*>(parent.control_container);
    OleControlContainerAttachControlSite(*container, window);
}

void OleControlContainerAttachControlSite(MfcOleControlContainerCompat& container,
    MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return;
    }
    auto found = container.sites.find(window.window);
    if (found == container.sites.end() || found->second == nullptr) {
        return;
    }
    MfcOleControlSiteCompat* site = found->second;
    if (site->control_window != nullptr &&
        site->control_window->control_site == site) {
        site->control_window->control_site = nullptr;
    }
    window.control_site = site;
    site->control_window = &window;
}

void UpdateMfcAuxDataSysColors(MfcAuxDataCompat& aux_data) {
    aux_data.button_face = GetSysColor(COLOR_BTNFACE);
    aux_data.button_shadow = GetSysColor(COLOR_BTNSHADOW);
    aux_data.button_highlight = GetSysColor(COLOR_BTNHIGHLIGHT);
    aux_data.button_text = GetSysColor(COLOR_BTNTEXT);
    aux_data.window_frame = GetSysColor(COLOR_WINDOWFRAME);
    aux_data.button_face_brush = GetSysColorBrush(COLOR_BTNFACE);
    aux_data.window_frame_brush = GetSysColorBrush(COLOR_WINDOWFRAME);
}

void UpdateMfcAuxDataSysMetrics(MfcAuxDataCompat& aux_data) {
    aux_data.icon_width = GetSystemMetrics(SM_CXICON);
    aux_data.icon_height = GetSystemMetrics(SM_CYICON);
    HDC dc = GetDC(nullptr);
    if (dc != nullptr) {
        aux_data.pixels_per_inch_x = GetDeviceCaps(dc, LOGPIXELSX);
        aux_data.pixels_per_inch_y = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(nullptr, dc);
    }
    aux_data.metrics_initialized = true;
}

bool CStringLoadString(MfcCStringCompat& text, UINT resource_id) {
    char stack_buffer[256]{};
    int length = AfxLoadStringCompat(resource_id, stack_buffer,
        static_cast<int>(sizeof(stack_buffer)));
    if (length <= 0) {
        text.text.clear();
        return false;
    }
    if (sizeof(stack_buffer) - static_cast<std::size_t>(length) >= 3) {
        text.text.assign(stack_buffer, static_cast<std::size_t>(length));
        return true;
    }

    int capacity = static_cast<int>(sizeof(stack_buffer));
    do {
        capacity += 256;
        char* buffer = CStringGetBuffer(text, capacity - 1);
        length = AfxLoadStringCompat(resource_id, buffer, capacity);
    } while (length > 0 && capacity - length < 3);
    CStringReleaseBuffer(text, length);
    return length > 0;
}

int AfxLoadStringCompat(UINT resource_id, char* buffer, int max_count) {
    if (buffer == nullptr || max_count <= 0 ||
        !ValidateMemoryPointer(buffer, static_cast<std::size_t>(max_count), true)) {
        return 0;
    }
    HINSTANCE instance = GetModuleHandleA(nullptr);
    HRSRC block = FindResourceA(instance,
        MAKEINTRESOURCEA((resource_id >> 4) + 1), RT_STRING);
    if (block == nullptr) {
        buffer[0] = '\0';
        return 0;
    }
    const int length = LoadStringA(instance, resource_id, buffer, max_count);
    if (length == 0) {
        buffer[0] = '\0';
    }
    return length;
}

bool CStringExtractSubString(MfcCStringCompat& text, const char* full_string,
    int substring_index, char separator) {
    if (full_string == nullptr || substring_index < 0) {
        text.text.clear();
        return false;
    }

    const char* begin = full_string;
    while (substring_index > 0) {
        const char* next = std::strchr(begin, separator);
        if (next == nullptr) {
            text.text.clear();
            return false;
        }
        begin = next + 1;
        --substring_index;
    }

    const char* end = std::strchr(begin, separator);
    if (end == nullptr) {
        text.text.assign(begin);
    } else {
        text.text.assign(begin, static_cast<std::size_t>(end - begin));
    }
    return true;
}

MfcCommandTargetCompat& ConstructCmdTarget(MfcCommandTargetCompat& target) {
    target.runtime_class = GetCObjectRuntimeClass();
    target.reference_count = 1;
    target.dispatch_map = nullptr;
    target.connection_map = nullptr;
    target.automation_enabled = true;
    target.final_release_enabled = true;
    target.message_map = nullptr;
    target.routing_target = nullptr;
    target.owner = nullptr;
    return target;
}

void DestroyCmdTarget(MfcCommandTargetCompat& target) {
    if (target.reference_count > 1) {
        AfxTraceOutput("Warning: destroying CCmdTarget with reference count %d.\n",
            target.reference_count);
    }
    target.dispatch_map = nullptr;
    target.connection_map = nullptr;
    target.message_map = nullptr;
    target.routing_target = nullptr;
    target.owner = nullptr;
}

void OnFinalRelease(MfcCommandTargetCompat& target) {
    target.reference_count = 0;
    if (target.final_release_enabled) {
        target.final_release_enabled = false;
        AfxOleUnlockApp();
    }
    DestroyCmdTarget(target);
}

bool DispatchCmdMsg(MfcCommandTargetCompat& target, UINT id, int code,
    void* handler, void* extra, UINT signature,
    MfcCommandHandlerInfoCompat* handler_info) {
    if (handler_info != nullptr) {
        handler_info->target = &target;
        handler_info->handler = handler;
        return true;
    }
    if (handler == nullptr) {
        return false;
    }

    auto callback = reinterpret_cast<MfcCommandHandlerCallback>(handler);
    switch (signature) {
    case 0x0c:
    case 0x0d:
    case 0x23:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2c:
    case 0x2d:
    case 0x2e:
    case 0x2f:
    default:
        return callback(target, id, code, extra);
    }
}

bool CmdTargetOnCmdMsg(MfcCommandTargetCompat& target, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    UINT message = WM_COMMAND;
    UINT notify_code = static_cast<UINT>(code);
    if (code != -1) {
        const UINT packed = static_cast<UINT>(code);
        const UINT high = packed >> 16;
        message = high == 0 ? WM_COMMAND : high;
        notify_code = packed & 0xffffU;
    }

    for (const MfcMessageMapCompat* map = target.message_map; map != nullptr;
         map = map->base) {
        const MfcMessageMapEntryCompat* entry =
            FindMessageMapEntry(map->entries, message, notify_code, id);
        if (entry == nullptr) {
            continue;
        }
        if (handler_info != nullptr) {
            handler_info->target = &target;
            handler_info->handler = entry->handler;
            return true;
        }
        if (entry->callback != nullptr &&
            entry->callback(target, id, code, extra)) {
            return true;
        }
        return DispatchCmdMsg(target, id, code, entry->handler, extra,
            entry->signature, nullptr);
    }

    if (target.routing_target != nullptr) {
        return CmdTargetOnCmdMsg(*target.routing_target, id, code, extra,
            handler_info);
    }
    return false;
}

bool CmdTargetIsInvokeAllowed() {
    return true;
}

bool CmdTargetGetDispatchIIDDefault(void* iid) {
    (void)iid;
    return false;
}

unsigned CmdTargetGetTypeInfoCountDefault() {
    return 0;
}

bool CmdTargetGetTypeLibDefault(void* type_lib) {
    (void)type_lib;
    return false;
}

long CmdTargetGetTypeInfoOfGuidDefault() {
    return static_cast<long>(0x80029c4aUL);
}

void AfxBeginWaitCursor() {
    if (MfcWinAppCompat* app = AfxGetAppCompat()) {
        WinAppDoWaitCursor(*app, 1);
        return;
    }
    if (g_wait_cursor_count++ == 0) {
        g_previous_wait_cursor = SetCursor(LoadCursorA(nullptr, IDC_WAIT));
    } else {
        SetCursor(LoadCursorA(nullptr, IDC_WAIT));
    }
}

void AfxEndWaitCursor() {
    if (MfcWinAppCompat* app = AfxGetAppCompat()) {
        WinAppDoWaitCursor(*app, -1);
        return;
    }
    if (g_wait_cursor_count > 0) {
        --g_wait_cursor_count;
    }
    if (g_wait_cursor_count == 0) {
        SetCursor(g_previous_wait_cursor != nullptr
            ? g_previous_wait_cursor : LoadCursorA(nullptr, IDC_ARROW));
        g_previous_wait_cursor = nullptr;
    }
}

void AfxRestoreWaitCursor() {
    if (MfcWinAppCompat* app = AfxGetAppCompat()) {
        WinAppDoWaitCursor(*app, 0);
        return;
    }
    if (g_wait_cursor_count > 0) {
        SetCursor(LoadCursorA(nullptr, IDC_WAIT));
    }
}

MfcRuntimeClassCompat* GetCmdTargetRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CCmdTarget", static_cast<int>(sizeof(MfcCommandTargetCompat)), 0xffff,
        +[]() -> void* {
            auto* target = new MfcCommandTargetCompat();
            ConstructCmdTarget(*target);
            return target;
        },
        GetCObjectRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetCmdTargetMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NULL, 0, 0, 0, nullptr, 0, nullptr},
    };
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

void* GetCmdTargetDispatchMap() {
    return nullptr;
}

void* GetCmdTargetInterfaceMap() {
    return nullptr;
}

bool CmdTargetDefaultTrue() {
    return true;
}

bool CmdTargetDefaultFalse() {
    return false;
}

bool CmdTargetDispatchDefaultFalse() {
    return false;
}

bool CmdTargetInterfaceDefaultFalse() {
    return false;
}

void* GetCmdTargetRuntimeClassThunk() {
    return GetCmdTargetRuntimeClass();
}

void* GetCmdTargetMessageMapThunk() {
    return const_cast<MfcMessageMapCompat*>(GetCmdTargetMessageMap());
}

void* GetThreadStateCurrentWindow() {
    return GetThreadStateCurrentWindowSlot();
}

void* GetThreadStateRoutingFrame() {
    return GetThreadStateRoutingFrameSlot();
}

MfcCmdUICompat& ConstructCmdUI(MfcCmdUICompat& cmd_ui) {
    cmd_ui = MfcCmdUICompat{};
    return cmd_ui;
}

void CmdUIEnable(MfcCmdUICompat& cmd_ui, bool enabled) {
    if (cmd_ui.menu == nullptr) {
        if (cmd_ui.other != nullptr) {
            CWndEnableWindow(*cmd_ui.other, enabled ? TRUE : FALSE);
        }
    } else if (!cmd_ui.sub_menu) {
        const UINT state = MF_BYPOSITION |
            (enabled ? MF_ENABLED : (MF_DISABLED | MF_GRAYED));
        EnableMenuItem(cmd_ui.menu, cmd_ui.index, state);
    }
    cmd_ui.changed = true;
}

void CmdUISetCheck(MfcCmdUICompat& cmd_ui, int check) {
    if (cmd_ui.menu == nullptr) {
        if (cmd_ui.other != nullptr && cmd_ui.other->window != nullptr) {
            SendMessageA(cmd_ui.other->window, BM_SETCHECK,
                static_cast<WPARAM>(check), 0);
        }
        return;
    }
    if (!cmd_ui.sub_menu) {
        CheckMenuItem(cmd_ui.menu, cmd_ui.index, MF_BYPOSITION |
            (check != 0 ? MF_CHECKED : MF_UNCHECKED));
    }
}

void CmdUISetRadio(MfcCmdUICompat& cmd_ui, bool enabled) {
    CmdUISetCheck(cmd_ui, enabled ? 1 : 0);
}

void CmdUISetText(MfcCmdUICompat& cmd_ui, const char* text) {
    if (text == nullptr) {
        return;
    }
    if (cmd_ui.menu == nullptr) {
        if (cmd_ui.other != nullptr && cmd_ui.other->window != nullptr) {
            SetWindowTextA(cmd_ui.other->window, text);
        }
        return;
    }
    if (!cmd_ui.sub_menu) {
        ModifyMenuA(cmd_ui.menu, cmd_ui.index, MF_BYPOSITION | MF_STRING,
            cmd_ui.id, text);
    }
}

bool CmdUIDoUpdate(MfcCmdUICompat& cmd_ui, MfcCommandTargetCompat& target,
    bool disable_if_no_handler) {
    AfxAssertValidObject(&target, "cmdtarg.cpp", 0x2e2);
    if (cmd_ui.id == 0 || LOWORD(cmd_ui.id) == 0xffff) {
        return true;
    }

    cmd_ui.changed = false;
    const bool handled = CmdTargetOnCmdMsg(target, cmd_ui.id, -1, &cmd_ui,
        nullptr);
    if (!handled && cmd_ui.continue_routing) {
        AfxTraceOutput("Warning: command UI continued routing without a handler.\n");
    }
    if (!handled && disable_if_no_handler && !cmd_ui.changed) {
        MfcCommandHandlerInfoCompat info{};
        const bool has_handler = CmdTargetOnCmdMsg(target, cmd_ui.id, 0,
            &cmd_ui, &info);
        if (!has_handler) {
            AfxTraceOutput("No handler for command ID 0x%04X.\n", cmd_ui.id);
        }
        CmdUIEnable(cmd_ui, has_handler);
    }
    return handled;
}

MfcCommandTargetCompat* DeleteCmdTargetScalarDtor(
    MfcCommandTargetCompat* target, unsigned flags) {
    if (target == nullptr) {
        return nullptr;
    }
    DestroyCmdTarget(*target);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(target);
    }
    return target;
}

MfcRuntimeClassCompat* GetDialogRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CDialog", static_cast<int>(sizeof(MfcDialogCompat)), 0xffff,
        +[]() -> void* {
            auto* dialog = new MfcDialogCompat();
            ConstructDialogDefault(*dialog);
            return dialog;
        },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcDialogCompat& ConstructDialogDefault(MfcDialogCompat& dialog) {
    ConstructCWnd(dialog);
    dialog.runtime_class = GetDialogRuntimeClass();
    dialog.template_name = nullptr;
    dialog.template_id = 0;
    dialog.template_instance = nullptr;
    dialog.parent = nullptr;
    dialog.init_param = nullptr;
    dialog.modal_resource = nullptr;
    dialog.modal_template = nullptr;
    dialog.dialog_init = nullptr;
    dialog.disabled_owner = nullptr;
    dialog.occ_dialog_info = nullptr;
    dialog.help_id = 0;
    dialog.modal_template_initialized = false;
    dialog.creation_failed = false;
    return dialog;
}

void DestroyDialog(MfcDialogCompat& dialog) {
    if (dialog.window != nullptr && IsWindow(dialog.window)) {
        AfxTraceOutput("Warning: destroying CDialog while HWND is still attached.\n");
        CWndDestroyWindow(dialog);
    }
    DestroyCWndCompat(dialog);
    dialog.template_name = nullptr;
    dialog.template_id = 0;
    dialog.template_instance = nullptr;
    dialog.parent = nullptr;
    dialog.init_param = nullptr;
    dialog.modal_resource = nullptr;
    dialog.modal_template = nullptr;
    dialog.dialog_init = nullptr;
    dialog.disabled_owner = nullptr;
    dialog.occ_dialog_info = nullptr;
    dialog.help_id = 0;
    dialog.modal_template_initialized = false;
    dialog.creation_failed = false;
}

bool DialogPreTranslateMessage(MfcDialogCompat& dialog, MSG& message) {
    if (dialog.window == nullptr || !IsWindow(dialog.window)) {
        return false;
    }
    if (message.message == WM_KEYDOWN &&
        (message.wParam == VK_ESCAPE || message.wParam == VK_CANCEL)) {
        HWND cancel = GetDlgItem(dialog.window, IDCANCEL);
        if (cancel == nullptr || IsWindowEnabled(cancel) != FALSE) {
            SendMessageA(dialog.window, WM_COMMAND, IDCANCEL, 0);
            return true;
        }
    }
    return CWndPreTranslateInput(dialog, message);
}

bool PreTranslateMessage(MfcDialogCompat& dialog, MSG& message) {
    return DialogPreTranslateMessage(dialog, message);
}

LRESULT OnHelpHitTest(MfcDialogCompat& dialog, UINT, LPARAM) {
    return static_cast<LRESULT>(dialog.help_id);
}

LRESULT OnHelpHitTest(MfcFrameWndCompat& frame, UINT, LPARAM) {
    return frame.help_context == 0
        ? 0 : static_cast<LRESULT>(frame.help_context + 0x20000U);
}

bool DialogOnCmdMsg(MfcDialogCompat& dialog, UINT id, int code, void* extra,
    MfcCommandHandlerInfoCompat* handler_info) {
    MfcCommandTargetCompat target{};
    ConstructCmdTarget(target);
    target.message_map = GetCmdTargetMessageMap();
    if (CmdTargetOnCmdMsg(target, id, code, extra, handler_info)) {
        return true;
    }
    if ((code == 0 || code == -1) && (id & 0x8000U) != 0 && id < 0xf000U) {
        MfcWinThreadCompat* thread = AfxGetThreadCompat();
        if (thread != nullptr) {
            (void)thread;
        }
    }
    (void)dialog;
    return false;
}

bool DialogCreate(MfcDialogCompat& dialog, LPCSTR template_name,
    MfcCWndCompat* parent) {
    if (template_name == nullptr) {
        return false;
    }
    dialog.template_name = template_name;
    if (IS_INTRESOURCE(template_name) && dialog.template_id == 0) {
        dialog.template_id = LOWORD(reinterpret_cast<ULONG_PTR>(template_name));
    }

    HINSTANCE instance = GetModuleHandleA(nullptr);
    HRSRC resource = FindResourceA(instance, template_name, RT_DIALOG);
    if (resource == nullptr) {
        dialog.creation_failed = true;
        return false;
    }
    HGLOBAL data = LoadResource(instance, resource);
    if (data == nullptr) {
        dialog.creation_failed = true;
        return false;
    }
    const bool created = DialogCreateIndirectFromResource(dialog, data, parent,
        instance);
    FreeResource(data);
    return created;
}

bool DialogCreateIndirectResource(MfcDialogCompat& dialog, HGLOBAL resource,
    MfcCWndCompat* parent) {
    return DialogCreateIndirectFromResource(dialog, resource, parent, nullptr);
}

bool DialogCreateIndirectFromResource(MfcDialogCompat& dialog, HGLOBAL resource,
    MfcCWndCompat* parent, HINSTANCE instance) {
    if (resource == nullptr) {
        dialog.creation_failed = true;
        return false;
    }
    const auto* dialog_template =
        static_cast<const DLGTEMPLATE*>(LockResource(resource));
    if (dialog_template == nullptr) {
        dialog.creation_failed = true;
        return false;
    }
    const bool created = DialogCreateIndirectCore(dialog, dialog_template,
        parent, nullptr, instance);
    UnlockResource(resource);
    return created;
}

bool DialogCreateIndirectCore(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent, void* init_param,
    HINSTANCE instance) {
    if (dialog_template == nullptr) {
        dialog.creation_failed = true;
        return false;
    }
    if (parent == nullptr) {
        MfcWinThreadCompat* thread = AfxGetThreadCompat();
        parent = thread != nullptr && thread->main_window != nullptr
            ? CWndFromHandle(thread->main_window) : nullptr;
    }
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    dialog.init_param = init_param;
    return DialogCreateIndirectOrModal(dialog, dialog_template, parent,
        instance);
}

bool DialogInitModalIndirectResource(MfcDialogCompat& dialog,
    LPCSTR template_name, MfcCWndCompat* parent) {
    dialog.template_name = template_name;
    if (IS_INTRESOURCE(template_name)) {
        dialog.template_id = LOWORD(reinterpret_cast<ULONG_PTR>(template_name));
    }
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    dialog.modal_template_initialized = template_name != nullptr;
    return dialog.modal_template_initialized;
}

bool DialogInitModalIndirect(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent) {
    dialog.template_name = nullptr;
    dialog.template_id = 0;
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    dialog.init_param = const_cast<DLGTEMPLATE*>(dialog_template);
    dialog.modal_template_initialized = dialog_template != nullptr;
    return dialog.modal_template_initialized;
}

bool DialogCreateIndirectOrModal(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent,
    HINSTANCE instance) {
    if (dialog_template == nullptr) {
        dialog.creation_failed = true;
        return false;
    }
    if (instance == nullptr) {
        instance = GetModuleHandleA(nullptr);
    }
    AfxDeferRegisterClass(0x10);
    AfxDeferRegisterClass(0x3c000);

    dialog.modal_result = -1;
    dialog.modal_flags |= 0x10;
    AfxHookWindowCreate(dialog);
    HWND parent_window = parent == nullptr ? nullptr : parent->window;
    HWND created = CreateDialogIndirectParamA(instance, dialog_template,
        parent_window, reconstructed_dialog_proc,
        reinterpret_cast<LPARAM>(dialog.init_param));
    const DWORD last_error = created == nullptr ? GetLastError() : ERROR_SUCCESS;
    return DialogCreateIndirectCleanup(dialog, created, nullptr, last_error);
}

bool DialogCreateIndirectCleanup(MfcDialogCompat& dialog, HWND created,
    HGLOBAL global_template, DWORD last_error) {
    if (global_template != nullptr) {
        GlobalUnlock(global_template);
        GlobalFree(global_template);
    }
    if (!AfxUnhookWindowCreate()) {
        CWndDefaultAndReleaseControlSite(dialog);
    }
    if (created == nullptr) {
        dialog.creation_failed = true;
        if ((dialog.modal_flags & 0x10U) != 0) {
            AfxTraceOutput("Warning: Dialog creation failed, error %lu.\n",
                last_error);
        }
        return false;
    }
    if (dialog.window == nullptr) {
        AttachCWndHandle(dialog, created);
    }
    dialog.creation_failed = false;
    return true;
}

bool DialogSetOccDialogInfo(MfcDialogCompat& dialog, void* occ_info) {
    dialog.occ_dialog_info = occ_info;
    return true;
}

MfcDialogCompat& ConstructDialogWithTemplateName(MfcDialogCompat& dialog,
    LPCSTR template_name, MfcCWndCompat* parent) {
    ConstructDialogDefault(dialog);
    dialog.template_name = template_name;
    if (IS_INTRESOURCE(template_name)) {
        dialog.template_id = LOWORD(reinterpret_cast<ULONG_PTR>(template_name));
    }
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    return dialog;
}

MfcDialogCompat& ConstructDialogWithTemplateId(MfcDialogCompat& dialog,
    UINT template_id, MfcCWndCompat* parent) {
    ConstructDialogDefault(dialog);
    dialog.template_id = template_id;
    dialog.template_name = MAKEINTRESOURCEA(template_id);
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    return dialog;
}

bool DialogInitModalIndirectHandle(MfcDialogCompat& dialog, HGLOBAL resource,
    MfcCWndCompat* parent) {
    if (dialog.template_name != nullptr || dialog.modal_resource != nullptr ||
        resource == nullptr) {
        return false;
    }
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    dialog.modal_resource = resource;
    dialog.modal_template = nullptr;
    dialog.template_instance = GetModuleHandleA(nullptr);
    dialog.modal_template_initialized = true;
    return true;
}

bool DialogInitModalIndirectPointer(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent,
    void* dialog_init) {
    if (dialog.template_name != nullptr || dialog.modal_template != nullptr ||
        dialog_template == nullptr) {
        return false;
    }
    dialog.parent = parent == nullptr ? nullptr : parent->window;
    dialog.modal_template = dialog_template;
    dialog.dialog_init = dialog_init;
    dialog.init_param = dialog_init;
    dialog.modal_template_initialized = true;
    return true;
}

HWND DialogPreModal(MfcDialogCompat& dialog) {
    if (dialog.window != nullptr) {
        return nullptr;
    }
    if (AfxGetAppCompat() != nullptr) {
        AfxEnableModelessCompat(false);
    }
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    HWND owner = dialog.parent;
    if (owner == nullptr && thread != nullptr) {
        owner = thread->main_window;
    }
    if (owner != nullptr) {
        while ((GetWindowLongA(owner, GWL_STYLE) & WS_CHILD) != 0) {
            owner = GetParent(owner);
        }
        owner = GetLastActivePopup(owner);
    }
    dialog.disabled_owner = owner;
    AfxHookWindowCreate(dialog);
    return owner;
}

void DialogPostModal(MfcDialogCompat& dialog) {
    AfxUnhookWindowCreate();
    DetachCWndHandle(dialog);
    if (dialog.disabled_owner != nullptr &&
        IsWindow(dialog.disabled_owner) != FALSE) {
        EnableWindow(dialog.disabled_owner, TRUE);
    }
    dialog.disabled_owner = nullptr;
    if (AfxGetAppCompat() != nullptr) {
        AfxEnableModelessCompat(true);
    }
}

INT_PTR DialogDoModal(MfcDialogCompat& dialog) {
    const DLGTEMPLATE* dialog_template = dialog.modal_template;
    HGLOBAL resource = dialog.modal_resource;
    bool resource_locked = false;
    HINSTANCE instance = dialog.template_instance != nullptr
        ? dialog.template_instance : GetModuleHandleA(nullptr);

    if (dialog.template_name != nullptr) {
        HRSRC info = FindResourceA(instance, dialog.template_name, RT_DIALOG);
        resource = info == nullptr ? nullptr : LoadResource(instance, info);
    }
    if (resource != nullptr) {
        dialog_template = static_cast<const DLGTEMPLATE*>(LockResource(resource));
        resource_locked = dialog_template != nullptr;
    }
    if (dialog_template == nullptr) {
        return -1;
    }

    HWND owner = DialogPreModal(dialog);
    if (owner != nullptr && IsWindowEnabled(owner) != FALSE) {
        EnableWindow(owner, FALSE);
    }
    dialog.init_param = dialog.dialog_init;
    if (DialogCreateIndirectOrModal(dialog, dialog_template,
            owner == nullptr ? nullptr : CWndFromHandle(owner), instance)) {
        if ((dialog.modal_flags & 0x10U) != 0) {
            const unsigned loop_flags =
                (CWndGetStyle(dialog) & WS_VISIBLE) != 0 ? 5U : 4U;
            dialog.modal_result = CWndRunModalLoop(dialog, loop_flags);
        }
        if (dialog.window != nullptr) {
            CWndSetWindowPos(dialog, nullptr, 0, 0, 0, 0,
                SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_HIDEWINDOW);
        }
    }
    return DialogDoModalCleanup(dialog, owner, resource, resource_locked);
}

INT_PTR DialogDoModalCleanup(MfcDialogCompat& dialog, HWND owner,
    HGLOBAL resource, bool resource_locked) {
    if (owner != nullptr && IsWindow(owner) != FALSE) {
        EnableWindow(owner, TRUE);
        if (GetActiveWindow() == dialog.window) {
            SetActiveWindow(owner);
        }
    }
    DialogPostModal(dialog);
    if (resource_locked && resource != nullptr) {
        UnlockResource(resource);
    }
    if (dialog.template_name != nullptr && resource != nullptr) {
        FreeResource(resource);
    }
    return dialog.modal_result;
}

void DialogEndDialog(MfcDialogCompat& dialog, INT_PTR result) {
    if (dialog.window == nullptr || IsWindow(dialog.window) == FALSE) {
        return;
    }
    if ((dialog.modal_flags & (0x08U | 0x10U)) != 0) {
        CWndEndModalLoop(dialog, static_cast<int>(result));
    }
    EndDialog(dialog.window, result);
}

void DialogDefaultNoop() {
}

long DialogHandleInitDialog(MfcDialogCompat& dialog) {
    CWndOnInitDialogDefault(dialog);
    if (dialog.occ_dialog_info != nullptr) {
        const char* failure_message =
            "Warning: CreateDlgControls failed during dialog init.\n";
        const bool created = dialog.modal_template != nullptr
            ? OccCreateDialogControlsFromTemplate(dialog,
                  dialog.modal_template, dialog.occ_dialog_info,
                  failure_message)
            : OccCreateDialogControlsFromResource(dialog,
                  dialog.template_name, dialog.occ_dialog_info,
                  failure_message);
        if (!created) {
            DialogEndDialog(dialog, -1);
            return 0;
        }
    }
    LRESULT result = DefWindowProcA(dialog.window, WM_INITDIALOG, 0, 0);
    if (result != 0 && (dialog.modal_flags & 0x100U) != 0) {
        HWND focus = GetFocus();
        if (focus != nullptr) {
            SetFocus(focus);
            result = 0;
        }
    }
    return static_cast<long>(result);
}

bool DialogRouteHelpCommand() {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->main_window != nullptr) {
        SendMessageA(thread->main_window, WM_COMMAND, kMfcHelpCommand, 0);
        return true;
    }
    return false;
}

long OnCommandHelp(MfcDialogCompat& dialog, UINT command, long help_id) {
    (void)command;
    if (help_id == 0 && dialog.help_id != 0) {
        help_id = static_cast<long>(dialog.help_id + 0x20000UL);
    }
    if (help_id == 0) {
        return 0;
    }
    CWndWinHelp(dialog, static_cast<ULONG_PTR>(help_id), HELP_CONTEXT);
    return 1;
}

long OnCommandHelp(MfcPropertySheetCompat& sheet, UINT command, long help_id) {
    (void)command;
    if (help_id == 0 && sheet.initial_page < sheet.pages.size()) {
        MfcPropertyPageCompat* page = sheet.pages[sheet.initial_page];
        if (page != nullptr && page->help_id != 0) {
            help_id = static_cast<long>(page->help_id + 0x20000UL);
        }
    }
    if (help_id == 0) {
        return DialogRouteHelpCommand() ? 1 : 0;
    }
    CWndWinHelp(sheet, static_cast<ULONG_PTR>(help_id), HELP_CONTEXT);
    return 1;
}

long OnCommandHelp(MfcFrameWndCompat& frame, UINT command, long help_id) {
    (void)command;
    if (help_id == 0) {
        if (IsTracking(frame)) {
            if (frame.tracking_message_id != 0) {
                help_id = static_cast<long>(frame.tracking_message_id + 0x10000U);
            }
        } else if (frame.help_context != 0) {
            help_id = static_cast<long>(frame.help_context + 0x20000U);
        }
    }
    if (help_id == 0) {
        return 0;
    }
    MfcWinAppCompat* app = AfxGetAppCompat();
    const char* help_file = app == nullptr || app->help_file_path.empty()
        ? nullptr : app->help_file_path.c_str();
    CWndWinHelp(frame, static_cast<ULONG_PTR>(help_id), HELP_CONTEXT,
        help_file);
    return 1;
}

void DialogOnSetFontDefault() {
}

bool DialogOnInitDialog(MfcDialogCompat& dialog) {
    const bool initialized = dialog.init_param != nullptr
        ? ExecuteDlgInitStream(dialog, dialog.init_param)
        : (dialog.template_name == nullptr ||
            ExecuteDlgInitResource(dialog, dialog.template_name));
    if (!initialized) {
        AfxTraceOutput("Warning: ExecuteDlgInit failed during dialog init.\n");
        DialogEndDialog(dialog, -1);
        return false;
    }
    if (!CWndUpdateData(dialog, false)) {
        AfxTraceOutput("Warning: UpdateData failed during dialog init.\n");
        DialogEndDialog(dialog, -1);
        return false;
    }
    HWND help = GetDlgItem(dialog.window, static_cast<int>(kMfcHelpCommand));
    if (help != nullptr) {
        ShowWindow(help, DialogRouteHelpCommand() ? SW_SHOW : SW_HIDE);
    }
    return true;
}

void DialogOnOK(MfcDialogCompat& dialog) {
    if (!CWndUpdateData(dialog, true)) {
        AfxTraceOutput("UpdateData failed during dialog termination.\n");
        return;
    }
    DialogEndDialog(dialog, IDOK);
}

void DialogOnCancel(MfcDialogCompat& dialog) {
    DialogEndDialog(dialog, IDCANCEL);
}

bool DialogTemplateIsSimpleTopLevel(MfcDialogCompat& dialog) {
    const DLGTEMPLATE* dialog_template = dialog.modal_template;
    HGLOBAL resource = dialog.modal_resource;
    bool locked = false;
    if (dialog.template_name != nullptr) {
        HINSTANCE instance = dialog.template_instance != nullptr
            ? dialog.template_instance : GetModuleHandleA(nullptr);
        HRSRC info = FindResourceA(instance, dialog.template_name, RT_DIALOG);
        resource = info == nullptr ? nullptr : LoadResource(instance, info);
    }
    if (resource != nullptr) {
        dialog_template = static_cast<const DLGTEMPLATE*>(LockResource(resource));
        locked = dialog_template != nullptr;
    }
    bool simple = true;
    if (dialog_template != nullptr) {
        simple = (dialog_template->style & (WS_POPUP | WS_CHILD | WS_VISIBLE)) == 0 &&
            dialog_template->x == 0 && dialog_template->y == 0;
    }
    if (locked && resource != nullptr) {
        UnlockResource(resource);
    }
    if (dialog.template_name != nullptr && resource != nullptr) {
        FreeResource(resource);
    }
    return simple;
}

void DialogOnCtlColorForward(MfcDialogCompat& dialog, HDC dc, HWND child) {
    CWndOnCtlColor(dialog, dc, child, CTLCOLOR_STATIC);
}

void DialogAssertValid(const MfcDialogCompat& dialog) {
    CWndAssertValid(dialog);
}

void DialogDump(const MfcDialogCompat& dialog) {
    CWndDump(dialog);
    AfxTraceOutput("m_lpszTemplateName = %p\n", dialog.template_name);
    AfxTraceOutput("m_hDialogTemplate = %p\n", dialog.modal_resource);
    AfxTraceOutput("m_lpDialogTemplate = %p\n", dialog.modal_template);
    AfxTraceOutput("m_nIDHelp = %p\n", reinterpret_cast<void*>(dialog.help_id));
}

namespace {

constexpr UINT kPropertyPageButtonBack = 0x3023;
constexpr UINT kPropertyPageButtonNext = 0x3024;
constexpr UINT kPropertySheetButtonIds[] = {
    IDOK, IDCANCEL, kPropertyPageButtonBack, kPropertyPageButtonNext};

LPCSTR property_template_from_id(UINT template_id) {
    return MAKEINTRESOURCEA(template_id & 0xffffU);
}

MfcPropertySheetCompat* property_sheet_from_parent(HWND window) {
    HWND parent = window == nullptr ? nullptr : GetParent(window);
    MfcCWndCompat* parent_window =
        parent == nullptr ? nullptr : CWndFromHandlePermanent(parent);
    if (parent_window == nullptr ||
        parent_window->runtime_class != GetPropertySheetRuntimeClass()) {
        return nullptr;
    }
    return static_cast<MfcPropertySheetCompat*>(parent_window);
}

void release_property_page_template(MfcPropertyPageCompat& page) {
    if (page.modified_template != nullptr) {
        GlobalUnlock(page.modified_template);
        GlobalFree(page.modified_template);
        page.modified_template = nullptr;
    }
}

void initialize_property_page_struct(MfcPropertyPageCompat& page,
    LPCSTR template_name, UINT caption_id) {
    page.page = PROPSHEETPAGEA{};
    page.page.dwSize = sizeof(PROPSHEETPAGEA);
    page.page.dwFlags = PSP_USECALLBACK;
    page.page.hInstance = GetModuleHandleA(nullptr);
    page.page.pszTemplate = template_name;
    page.page.pfnDlgProc = reconstructed_dialog_proc;
    page.page.lParam = reinterpret_cast<LPARAM>(&page);
    page.page.pfnCallback = PropertyPageCallback;

    page.template_name = template_name;
    page.template_id = IS_INTRESOURCE(template_name)
        ? LOWORD(reinterpret_cast<ULONG_PTR>(template_name)) : 0;
    page.template_instance = page.page.hInstance;
    page.original_template = template_name;
    page.original_template_id = page.template_id;

    page.caption.clear();
    page.header_title.clear();
    page.header_subtitle.clear();
    if (caption_id != 0) {
        MfcCStringCompat text;
        if (CStringLoadString(text, caption_id)) {
            page.caption = text.text;
            page.page.pszTitle = page.caption.c_str();
            page.page.dwFlags |= PSP_USETITLE;
        }
    }
    if (DialogRouteHelpCommand()) {
        page.page.dwFlags |= PSP_HASHELP;
    }
    page.first_set_active = true;
    page.modified = false;
}

const DLGTEMPLATE* load_property_page_template(
    const PROPSHEETPAGEA& sheet_page, HGLOBAL& resource) {
    resource = nullptr;
    if ((sheet_page.dwFlags & PSP_DLGINDIRECT) != 0) {
        return reinterpret_cast<const DLGTEMPLATE*>(sheet_page.pResource);
    }
    if (sheet_page.pszTemplate == nullptr) {
        return nullptr;
    }
    HINSTANCE instance = sheet_page.hInstance != nullptr
        ? sheet_page.hInstance : GetModuleHandleA(nullptr);
    HRSRC info = FindResourceA(instance, sheet_page.pszTemplate, RT_DIALOG);
    if (info == nullptr) {
        return nullptr;
    }
    resource = LoadResource(instance, info);
    return resource == nullptr
        ? nullptr : static_cast<const DLGTEMPLATE*>(LockResource(resource));
}

void prepare_property_sheet_pages(MfcPropertySheetCompat& sheet) {
    sheet.page_storage.clear();
    sheet.page_storage.reserve(sheet.pages.size());
    const UINT font_flags = sheet.header.dwFlags & 0x2020U;
    for (MfcPropertyPageCompat* page : sheet.pages) {
        if (page == nullptr) {
            continue;
        }
        sheet.page_storage.push_back(page->page);
        if (!page->header_title.empty()) {
            sheet.page_storage.back().pszHeaderTitle =
                page->header_title.c_str();
            sheet.page_storage.back().dwFlags |= PSP_USEHEADERTITLE;
        }
        if (!page->header_subtitle.empty()) {
            sheet.page_storage.back().pszHeaderSubTitle =
                page->header_subtitle.c_str();
            sheet.page_storage.back().dwFlags |= PSP_USEHEADERSUBTITLE;
        }
        PropertyPagePrepareSheetPageTemplate(*page, sheet.page_storage.back(),
            font_flags);
    }
    sheet.header.nPages = static_cast<UINT>(sheet.page_storage.size());
    sheet.header.ppsp = sheet.page_storage.empty()
        ? nullptr : sheet.page_storage.data();
}

} // namespace

UINT CALLBACK PropertyPageCallback(HWND hwnd, UINT message,
    PROPSHEETPAGEA* sheet_page) {
    if (message == PSPCB_RELEASE) {
        AfxUnhookWindowCreate();
        return 0;
    }
    if (message != PSPCB_CREATE || sheet_page == nullptr) {
        return 0;
    }
    auto* page = reinterpret_cast<MfcPropertyPageCompat*>(sheet_page->lParam);
    if (page == nullptr) {
        return 0;
    }
    AfxAssertValidObject(page, "dlgprop.cpp", 0x27);
    AfxHookWindowCreate(*page);
    (void)hwnd;
    return PropertyPageCallbackSuccess() ? 1U : 0U;
}

bool PropertyPageCallbackSuccess() {
    return true;
}

MfcRuntimeClassCompat* GetPropertyPageRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CPropertyPage", static_cast<int>(sizeof(MfcPropertyPageCompat)),
        0xffff,
        +[]() -> void* {
            auto* page = new MfcPropertyPageCompat();
            ConstructPropertyPageDefault(*page);
            return page;
        },
        GetDialogRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetPropertyPageMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_NOTIFY, 0, 0, 0, nullptr, 0, nullptr},
        {WM_CTLCOLORMSGBOX, 0, 0, 0, nullptr, 0, nullptr},
        {0, 0, 0, 0, nullptr, 0, nullptr}};
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

MfcPropertyPageCompat& ConstructPropertyPageWithTemplateId(
    MfcPropertyPageCompat& page, UINT template_id, UINT caption_id) {
    ConstructPropertyPageDefault(page);
    PropertyPageConstructByTemplateId(page, template_id, caption_id);
    return page;
}

MfcPropertyPageCompat& ConstructPropertyPageWithTemplateName(
    MfcPropertyPageCompat& page, LPCSTR template_name, UINT caption_id) {
    ConstructPropertyPageDefault(page);
    PropertyPageConstructByTemplateName(page, template_name, caption_id);
    return page;
}

void PropertyPageConstructByTemplateId(MfcPropertyPageCompat& page,
    UINT template_id, UINT caption_id) {
    if (template_id == 0) {
        AfxTraceOutput("Error: CPropertyPage constructed with ID 0.\n");
        return;
    }
    PropertyPageCommonConstruct(page, property_template_from_id(template_id),
        caption_id);
}

void PropertyPageConstructByTemplateName(MfcPropertyPageCompat& page,
    LPCSTR template_name, UINT caption_id) {
    if (template_name == nullptr) {
        AfxTraceOutput("Error: CPropertyPage constructed with NULL template.\n");
        return;
    }
    PropertyPageCommonConstruct(page, template_name, caption_id);
}

MfcPropertyPageCompat& ConstructPropertyPageDefault(
    MfcPropertyPageCompat& page) {
    ConstructDialogDefault(page);
    page.runtime_class = GetPropertyPageRuntimeClass();
    page.modified_template = nullptr;
    page.occ_dialog_info_page = nullptr;
    page.caption.clear();
    PropertyPageCommonConstruct(page, nullptr, 0);
    return page;
}

void PropertyPageCommonConstruct(MfcPropertyPageCompat& page,
    LPCSTR template_name, UINT caption_id) {
    release_property_page_template(page);
    if (page.occ_dialog_info_page != nullptr) {
        std::free(page.occ_dialog_info_page);
        page.occ_dialog_info_page = nullptr;
    }
    initialize_property_page_struct(page, template_name, caption_id);
}

void DestroyPropertyPage(MfcPropertyPageCompat& page) {
    PropertyPageCleanup(page);
    release_property_page_template(page);
    page.caption.clear();
    page.header_title.clear();
    page.header_subtitle.clear();
    DestroyDialog(page);
}

void PropertyPageCleanup(MfcPropertyPageCompat& page) {
    if (page.occ_dialog_info_page != nullptr) {
        std::free(page.occ_dialog_info_page);
        page.occ_dialog_info_page = nullptr;
    }
}

HGLOBAL PropertyPageCloneTemplateWithCurrentFont(const DLGTEMPLATE* source,
    UINT font_flags) {
    if (source == nullptr || font_flags == 0) {
        return nullptr;
    }
    const unsigned size = DialogTemplateSize(source);
    if (size == 0) {
        return nullptr;
    }
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size + 0x40);
    if (handle == nullptr) {
        ThrowMfcMemoryException();
    }
    void* locked = GlobalLock(handle);
    if (locked == nullptr) {
        GlobalFree(handle);
        ThrowMfcMemoryException();
    }
    std::memcpy(locked, source, size);
    GlobalUnlock(handle);

    MfcDialogTemplateCompat dialog_template;
    dialog_template.handle = handle;
    dialog_template.size = size;
    dialog_template.no_font =
        (MfcDialogTemplateStyle(source) & DS_SETFONT) == 0;
    if (!DialogTemplateSetSystemFont(dialog_template, 0)) {
        return DetachDialogTemplateHandle(dialog_template);
    }
    return DetachDialogTemplateHandle(dialog_template);
}

void PropertyPageCreateOccDialogInfo(MfcPropertyPageCompat& page,
    const DLGTEMPLATE* source) {
    PropertyPageCleanup(page);
    page.occ_dialog_info_page = std::malloc(8);
    if (page.occ_dialog_info_page == nullptr) {
        ThrowMfcMemoryException();
    }
    std::memset(page.occ_dialog_info_page, 0, 8);
    page.occ_dialog_info = page.occ_dialog_info_page;
    (void)source;
}

void PropertyPagePrepareSheetPageTemplate(MfcPropertyPageCompat& page,
    PROPSHEETPAGEA& sheet_page, UINT font_flags) {
    HGLOBAL resource = nullptr;
    const DLGTEMPLATE* source = load_property_page_template(sheet_page, resource);
    if (source == nullptr) {
        AfxTraceOutput("Warning: missing CPropertyPage dialog template.\n");
        return;
    }

    HGLOBAL modified = PropertyPageCloneTemplateWithCurrentFont(source,
        font_flags);
    if (modified != nullptr) {
        release_property_page_template(page);
        page.modified_template = modified;
        auto* locked = static_cast<const DLGTEMPLATE*>(GlobalLock(modified));
        if (locked != nullptr) {
            sheet_page.pResource = locked;
            sheet_page.dwFlags |= PSP_DLGINDIRECT;
        }
    } else if ((sheet_page.dwFlags & PSP_DLGINDIRECT) == 0) {
        sheet_page.pResource = source;
        sheet_page.dwFlags |= PSP_DLGINDIRECT;
    }
}

void PropertyPageCancelToClose(MfcPropertyPageCompat& page) {
    if (page.window == nullptr || !IsWindow(page.window)) {
        return;
    }
    HWND parent = GetParent(page.window);
    if (parent != nullptr) {
        SendMessageA(parent, PSM_CANCELTOCLOSE, 0, 0);
    }
}

void PropertyPageSetModified(MfcPropertyPageCompat& page, bool modified) {
    page.modified = modified;
    if (page.window == nullptr || !IsWindow(page.window)) {
        return;
    }
    HWND parent = GetParent(page.window);
    if (parent != nullptr) {
        SendMessageA(parent, modified ? PSM_CHANGED : PSM_UNCHANGED,
            reinterpret_cast<WPARAM>(page.window), 0);
    }
}

LRESULT PropertyPageQuerySiblings(MfcPropertyPageCompat& page, WPARAM wparam,
    LPARAM lparam) {
    if (page.window == nullptr || !IsWindow(page.window)) {
        return 0;
    }
    HWND parent = GetParent(page.window);
    return parent == nullptr ? 0 :
        SendMessageA(parent, PSM_QUERYSIBLINGS, wparam, lparam);
}

BOOL PropertyPageOnApply(MfcPropertyPageCompat& page) {
    AfxAssertValidObject(&page, "dlgprop.cpp", 299);
    PropertyPageOnOK(page);
    return TRUE;
}

void PropertyPageOnReset(MfcPropertyPageCompat& page) {
    AfxAssertValidObject(&page, "dlgprop.cpp", 0x133);
    PropertyPageOnCancel(page);
}

void PropertyPageOnOK(MfcPropertyPageCompat& page) {
    AfxAssertValidObject(&page, "dlgprop.cpp", 0x13a);
}

void PropertyPageOnCancel(MfcPropertyPageCompat& page) {
    AfxAssertValidObject(&page, "dlgprop.cpp", 0x13f);
}

BOOL PropertyPageOnSetActive(MfcPropertyPageCompat& page) {
    AfxAssertValidObject(&page, "dlgprop.cpp", 0x144);
    if (page.first_set_active) {
        page.first_set_active = false;
    } else {
        CWndUpdateData(page, false);
    }
    return TRUE;
}

BOOL PropertyPageOnKillActive(MfcPropertyPageCompat& page) {
    AfxAssertValidObject(&page, "dlgprop.cpp", 0x14f);
    const bool updated = CWndUpdateData(page, true);
    if (!updated) {
        AfxTraceOutput("UpdateData failed during page deactivation.\n");
    }
    return updated ? TRUE : FALSE;
}

BOOL PropertyPageOnQueryCancel() {
    return TRUE;
}

LRESULT PropertyPageOnWizardBack() {
    return 0;
}

LRESULT PropertyPageOnWizardNext() {
    return 0;
}

BOOL PropertyPageOnWizardFinish() {
    return TRUE;
}

LRESULT PropertyPageMapWizardResult(MfcPropertyPageCompat& page,
    LRESULT result) {
    if (result == 0 || result == -1) {
        return result;
    }
    MfcPropertySheetCompat* sheet = property_sheet_from_parent(page.window);
    if (sheet == nullptr) {
        return result;
    }
    for (MfcPropertyPageCompat* candidate : sheet->pages) {
        if (candidate != nullptr && candidate->original_template_id != 0 &&
            candidate->original_template_id == static_cast<UINT>(result)) {
            return reinterpret_cast<LRESULT>(candidate->page.pszTemplate);
        }
    }
    return result;
}

bool PropertyPageIsButtonEnabled(MfcPropertyPageCompat& page, int button_id) {
    if (page.window == nullptr) {
        return false;
    }
    HWND parent = GetParent(page.window);
    HWND button = parent == nullptr ? nullptr : GetDlgItem(parent, button_id);
    return button != nullptr && IsWindowEnabled(button) != FALSE;
}

bool PropertyPageOnNotify(MfcPropertyPageCompat& page, WPARAM control_id,
    NMHDR* notify, LRESULT& result) {
    if (notify == nullptr) {
        return false;
    }
    if (CWndOnNotify(page, control_id, notify, &result)) {
        return true;
    }
    HWND parent = page.window == nullptr ? nullptr : GetParent(page.window);
    if (notify->hwndFrom != page.window && notify->hwndFrom != parent) {
        return false;
    }

    switch (notify->code) {
    case PSN_QUERYCANCEL:
        result = PropertyPageOnQueryCancel() ? 0 : 1;
        return true;
    case PSN_WIZFINISH:
        result = PropertyPageOnWizardFinish() ? 0 : 1;
        return true;
    case PSN_WIZNEXT:
        if (PropertyPageIsButtonEnabled(page, kPropertyPageButtonNext)) {
            result = PropertyPageMapWizardResult(page,
                PropertyPageOnWizardNext());
        }
        return true;
    case PSN_WIZBACK:
        if (PropertyPageIsButtonEnabled(page, kPropertyPageButtonBack)) {
            result = PropertyPageMapWizardResult(page,
                PropertyPageOnWizardBack());
        }
        return true;
    case PSN_HELP:
        if (page.window != nullptr) {
            SendMessageA(page.window, WM_COMMAND, kMfcHelpCommand, 0);
        }
        return true;
    case PSN_RESET:
        PropertyPageOnReset(page);
        return true;
    case PSN_APPLY:
        result = PropertyPageOnApply(page) ? PSNRET_NOERROR :
            PSNRET_INVALID_NOCHANGEPAGE;
        return true;
    case PSN_KILLACTIVE:
        result = PropertyPageOnKillActive(page) ? 0 : 1;
        return true;
    case PSN_SETACTIVE:
        result = PropertyPageOnSetActive(page) ? 0 : -1;
        return true;
    default:
        return false;
    }
}

bool PropertyPagePreTranslateMessage(MfcPropertyPageCompat& page,
    MSG& message) {
    return CWndPreTranslateInput(page, message);
}

LRESULT PropertyPageOnCtlColor(MfcPropertyPageCompat& page, HDC dc,
    HWND child, UINT type) {
    return CWndOnCtlColor(page, dc, child, type);
}

void PropertyPageAssertValid(const MfcPropertyPageCompat& page) {
    DialogAssertValid(page);
    if (page.page.dwSize != sizeof(PROPSHEETPAGEA)) {
        AfxTraceOutput("CPropertyPage has an invalid PROPSHEETPAGE size.\n");
    }
    if ((page.page.dwFlags & PSP_USECALLBACK) == 0) {
        AfxTraceOutput("CPropertyPage missing PSP_USECALLBACK.\n");
    }
}

void PropertyPageDump(const MfcPropertyPageCompat& page) {
    DialogDump(page);
    AfxTraceOutput("m_strCaption = %s\n", page.caption.c_str());
    AfxTraceOutput("m_psp.dwFlags = 0x%08lx\n", page.page.dwFlags);
}

void PropertyPageEndDialog(MfcPropertyPageCompat& page, int result) {
    MfcPropertySheetCompat* sheet = property_sheet_from_parent(page.window);
    if (sheet != nullptr) {
        CWndEndModalLoop(*sheet, result);
        if (sheet->window != nullptr && IsWindow(sheet->window)) {
            SendMessageA(sheet->window, PSM_PRESSBUTTON, PSBTN_CANCEL, 0);
        }
        return;
    }
    DialogEndDialog(page, result);
}

MfcRuntimeClassCompat* GetPropertySheetRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CPropertySheet", static_cast<int>(sizeof(MfcPropertySheetCompat)),
        0xffff,
        +[]() -> void* {
            auto* sheet = new MfcPropertySheetCompat();
            ConstructPropertySheetDefault(*sheet);
            return sheet;
        },
        GetCWndRuntimeClass(), nullptr};
    return &runtime_class;
}

const MfcMessageMapCompat* GetPropertySheetMessageMap() {
    static const MfcMessageMapEntryCompat entries[] = {
        {WM_COMMAND, 0, 0, 0, nullptr, 0, nullptr},
        {WM_NOTIFY, 0, 0, 0, nullptr, 0, nullptr},
        {0, 0, 0, 0, nullptr, 0, nullptr}};
    static const MfcMessageMapCompat map{nullptr, entries};
    return &map;
}

PROPSHEETHEADERA& PropertySheetHeader(MfcPropertySheetCompat& sheet) {
    return sheet.header;
}

MfcPropertyPageCompat* PropertySheetGetPageAt(
    MfcPropertySheetCompat& sheet, int index) {
    if (index < 0 || index >= static_cast<int>(sheet.pages.size())) {
        return nullptr;
    }
    return sheet.pages[static_cast<std::size_t>(index)];
}

void PropertyPageEnableHelpInline(MfcPropertyPageCompat& page) {
    page.page.dwFlags |= PSP_HASHELP;
}

void PropertySheetOnKickIdle(MfcPropertySheetCompat& sheet, int button_id,
    LPARAM lparam) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return;
    }
    HWND button = GetDlgItem(sheet.window, button_id);
    const bool button_usable = button != nullptr &&
        (GetWindowLongA(button, GWL_STYLE) & WS_VISIBLE) != 0 &&
        IsWindowEnabled(button) != FALSE;
    if (button_usable || (sheet.header.dwFlags & PSH_WIZARD) == 0) {
        DefWindowProcA(sheet.window, WM_NULL, 0, 0);
        return;
    }
    for (UINT fallback_id : kPropertySheetButtonIds) {
        HWND fallback = GetDlgItem(sheet.window, static_cast<int>(fallback_id));
        if (fallback != nullptr &&
            (GetWindowLongA(fallback, GWL_STYLE) & WS_VISIBLE) != 0 &&
            IsWindowEnabled(fallback) != FALSE) {
            HWND focus = GetFocus();
            if (focus != nullptr && IsWindowEnabled(focus) == FALSE) {
                SetFocus(fallback);
            }
            SendMessageA(sheet.window, WM_COMMAND, fallback_id, lparam);
            return;
        }
    }
    DefWindowProcA(sheet.window, WM_NULL, 0, 0);
}

MfcPropertySheetCompat& ConstructPropertySheetDefault(
    MfcPropertySheetCompat& sheet) {
    ConstructCWnd(sheet);
    sheet.runtime_class = GetPropertySheetRuntimeClass();
    sheet.pages.clear();
    sheet.page_storage.clear();
    sheet.caption.clear();
    PropertySheetCommonConstruct(sheet, nullptr, 0);
    return sheet;
}

MfcPropertySheetCompat& ConstructPropertySheetWithCaptionId(
    MfcPropertySheetCompat& sheet, UINT caption_id, MfcCWndCompat* parent,
    UINT selected_page) {
    ConstructPropertySheetDefault(sheet);
    PropertySheetConstructByCaptionId(sheet, caption_id, parent, selected_page);
    return sheet;
}

MfcPropertySheetCompat& ConstructPropertySheetWithCaptionName(
    MfcPropertySheetCompat& sheet, const char* caption, MfcCWndCompat* parent,
    UINT selected_page) {
    ConstructPropertySheetDefault(sheet);
    PropertySheetConstructByCaptionName(sheet, caption, parent, selected_page);
    return sheet;
}

void PropertySheetConstructByCaptionId(MfcPropertySheetCompat& sheet,
    UINT caption_id, MfcCWndCompat* parent, UINT selected_page) {
    if (caption_id == 0) {
        AfxTraceOutput("Error: CPropertySheet caption resource ID is 0.\n");
        return;
    }
    MfcCStringCompat text;
    if (CStringLoadString(text, caption_id)) {
        sheet.caption = text.text;
    }
    PropertySheetCommonConstruct(sheet, parent, selected_page);
}

void PropertySheetConstructByCaptionName(MfcPropertySheetCompat& sheet,
    const char* caption, MfcCWndCompat* parent, UINT selected_page) {
    sheet.caption = caption == nullptr ? "" : caption;
    PropertySheetCommonConstruct(sheet, parent, selected_page);
}

void PropertySheetCommonConstruct(MfcPropertySheetCompat& sheet,
    MfcCWndCompat* parent, UINT selected_page) {
    sheet.header = PROPSHEETHEADERA{};
    sheet.header.dwSize = sizeof(PROPSHEETHEADERA);
    sheet.header.dwFlags = PSH_PROPSHEETPAGE;
    sheet.header.hInstance = GetModuleHandleA(nullptr);
    sheet.header.pszCaption = sheet.caption.c_str();
    sheet.header.nStartPage = selected_page;
    sheet.parent_window = parent;
    sheet.initial_page = selected_page;
    sheet.stacked = true;
    sheet.modeless = false;
    sheet.pressed_button = 0;
    sheet.create_style = 0;
    sheet.create_ex_style = 0;
    if (DialogRouteHelpCommand()) {
        sheet.header.dwFlags |= PSH_HASHELP;
    }
}

void PropertySheetEnableStackedTabs(MfcPropertySheetCompat& sheet,
    bool stacked) {
    sheet.stacked = stacked;
}

void PropertySheetSetTitle(MfcPropertySheetCompat& sheet, const char* title,
    UINT style) {
    if ((style & ~1U) != 0) {
        AfxTraceOutput("Warning: unsupported CPropertySheet title style %u.\n",
            style);
        style &= 1U;
    }
    sheet.caption = title == nullptr ? "" : title;
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        sheet.header.pszCaption = sheet.caption.c_str();
        sheet.header.dwFlags = (sheet.header.dwFlags & ~PSH_PROPTITLE) | style;
        return;
    }
    SendMessageA(sheet.window, PSM_SETTITLEA, style,
        reinterpret_cast<LPARAM>(sheet.caption.c_str()));
}

void PropertySheetSetFinishText(MfcPropertySheetCompat& sheet,
    const char* text) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return;
    }
    SendMessageA(sheet.window, PSM_SETFINISHTEXTA, 0,
        reinterpret_cast<LPARAM>(text == nullptr ? "" : text));
}

void PropertySheetSetWizardButtons(MfcPropertySheetCompat& sheet,
    DWORD flags) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return;
    }
    PostMessageA(sheet.window, PSM_SETWIZBUTTONS, 0,
        static_cast<LPARAM>(flags));
}

MfcCWndCompat* PropertySheetGetTabControl(MfcPropertySheetCompat& sheet) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return nullptr;
    }
    HWND tab = reinterpret_cast<HWND>(
        SendMessageA(sheet.window, PSM_GETTABCONTROL, 0, 0));
    return tab == nullptr ? nullptr : CWndFromHandle(tab);
}

void PropertySheetPressButton(MfcPropertySheetCompat& sheet, int button) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return;
    }
    SendMessageA(sheet.window, PSM_PRESSBUTTON, static_cast<WPARAM>(button), 0);
}

bool PropertySheetUsesWizardFont(MfcPropertySheetCompat& sheet) {
    return (PropertySheetHeader(sheet).dwFlags & 0x2020U) != 0;
}

void DestroyPropertySheet(MfcPropertySheetCompat& sheet) {
    sheet.page_storage.clear();
    sheet.pages.clear();
    sheet.caption.clear();
    DestroyCWndCompat(sheet);
}

bool PropertySheetPreTranslateMessage(MfcPropertySheetCompat& sheet,
    MSG& message) {
    if (CWndPreTranslateInput(sheet, message)) {
        return true;
    }
    const bool control_down = GetAsyncKeyState(VK_CONTROL) < 0;
    if (message.message == WM_KEYDOWN && control_down &&
        (message.wParam == VK_TAB || message.wParam == VK_PRIOR ||
            message.wParam == VK_NEXT) && sheet.window != nullptr &&
        SendMessageA(sheet.window, PSM_ISDIALOGMESSAGE, 0,
            reinterpret_cast<LPARAM>(&message)) != 0) {
        return true;
    }
    return CWndPreTranslateInput(sheet, message);
}

bool PropertySheetOnCmdMsg(MfcPropertySheetCompat& sheet, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info) {
    MfcCommandTargetCompat target{};
    ConstructCmdTarget(target);
    target.message_map = GetCmdTargetMessageMap();
    if (CmdTargetOnCmdMsg(target, id, code, extra, handler_info)) {
        return true;
    }
    (void)sheet;
    return false;
}

MfcPropertyPageCompat* PropertySheetGetActivePage(
    MfcPropertySheetCompat& sheet) {
    if (sheet.pages.empty()) {
        return nullptr;
    }
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        const UINT index = sheet.header.nStartPage < sheet.pages.size()
            ? sheet.header.nStartPage : 0;
        return sheet.pages[index];
    }
    HWND page_hwnd = reinterpret_cast<HWND>(
        SendMessageA(sheet.window, PSM_GETCURRENTPAGEHWND, 0, 0));
    for (MfcPropertyPageCompat* page : sheet.pages) {
        if (page != nullptr && page->window == page_hwnd) {
            return page;
        }
    }
    return nullptr;
}

bool PropertySheetContinueModal(MfcPropertySheetCompat& sheet) {
    if (!CWndContinueModal(sheet)) {
        return false;
    }
    return sheet.window != nullptr && IsWindow(sheet.window) &&
        SendMessageA(sheet.window, PSM_GETCURRENTPAGEHWND, 0, 0) != 0;
}

int PropertySheetDoModal(MfcPropertySheetCompat& sheet) {
    AfxAssertValidObject(&sheet, "dlgprop.cpp", 0x300);
    if (sheet.window != nullptr) {
        AfxTraceOutput("Error: CPropertySheet already has an HWND.\n");
        return -1;
    }
    AfxDeferRegisterClass(0x10);
    AfxDeferRegisterClass(0x3c000);
    prepare_property_sheet_pages(sheet);

    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr) {
        AfxEnableModelessCompat(false);
    }

    HWND disabled_owner = nullptr;
    HWND owner = AfxGetSafeOwnerCompat(
        sheet.parent_window == nullptr ? nullptr : sheet.parent_window->window,
        &disabled_owner);
    sheet.header.hwndParent = owner;
    bool owner_disabled = false;
    if (owner != nullptr && IsWindowEnabled(owner) != FALSE) {
        EnableWindow(owner, FALSE);
        owner_disabled = true;
    }
    HWND capture = GetCapture();
    if (capture != nullptr) {
        SendMessageA(capture, WM_CANCELMODE, 0, 0);
    }

    sheet.modal_result = 0;
    sheet.modal_flags |= kMfcModalContinueFlag;
    AfxHookWindowCreate(sheet);
    sheet.header.dwFlags |= PSH_MODELESS;
    INT_PTR created = PropertySheetA(&sheet.header);
    const DWORD last_error = GetLastError();
    sheet.header.dwFlags &= ~PSH_MODELESS;
    AfxUnhookWindowCreate();

    int result = sheet.modal_result;
    if (created == 0 || created == -1) {
        AfxTraceOutput("PropertySheet() failed, GetLastError returned %lu.\n",
            last_error);
        sheet.modal_flags &= ~kMfcModalContinueFlag;
        result = -1;
    } else {
        HWND created_hwnd = reinterpret_cast<HWND>(created);
        if (sheet.window == nullptr && IsWindow(created_hwnd)) {
            AttachCWndHandle(sheet, created_hwnd);
        }
        if (sheet.window != nullptr && IsWindow(sheet.window)) {
            const unsigned loop_flags =
                (CWndGetStyle(sheet) & WS_VISIBLE) != 0 ? 5U : 4U;
            result = CWndRunModalLoop(sheet, loop_flags);
            CWndSetWindowPos(sheet, nullptr, 0, 0, 0, 0,
                SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_HIDEWINDOW);
        }
    }

    if (owner_disabled && owner != nullptr && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
    }
    if (owner != nullptr && GetActiveWindow() == sheet.window) {
        SetActiveWindow(owner);
    }
    if (sheet.window != nullptr) {
        DetachCWndHandle(sheet);
    }
    if (app != nullptr) {
        AfxEnableModelessCompat(true);
    }
    if (disabled_owner != nullptr && IsWindow(disabled_owner)) {
        EnableWindow(disabled_owner, TRUE);
    }
    return result;
}

int CALLBACK PropertySheetCreateCallback(HWND hwnd, UINT message,
    LPARAM lparam) {
    (void)hwnd;
    (void)lparam;
    return message == PSCB_PRECREATE ? 0 : 0;
}

bool PropertySheetCreateModeless(MfcPropertySheetCompat& sheet,
    MfcCWndCompat* parent, DWORD style, DWORD ex_style) {
    if (style == static_cast<DWORD>(-1)) {
        style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    }
    sheet.create_style = style;
    sheet.create_ex_style = ex_style;
    if (sheet.window != nullptr) {
        AfxTraceOutput("Error: CPropertySheet::Create called with an HWND.\n");
        return false;
    }
    AfxDeferRegisterClass(0x10);
    AfxDeferRegisterClass(0x3c000);
    prepare_property_sheet_pages(sheet);
    sheet.modeless = true;
    sheet.header.dwFlags |= PSH_MODELESS | PSH_USECALLBACK;
    sheet.header.pfnCallback = PropertySheetCreateCallback;
    sheet.header.hwndParent = parent == nullptr ? nullptr : parent->window;
    AfxHookWindowCreate(sheet);
    INT_PTR created = PropertySheetA(&sheet.header);
    const DWORD last_error = GetLastError();
    const bool unhooked = AfxUnhookWindowCreate();
    if (!unhooked) {
        CWndDefaultAndReleaseControlSite(sheet);
    }
    if (created == 0 || created == -1) {
        AfxTraceOutput("PropertySheet::Create failed, GetLastError=%lu.\n",
            last_error);
        return false;
    }
    HWND created_hwnd = reinterpret_cast<HWND>(created);
    if (sheet.window == nullptr && IsWindow(created_hwnd)) {
        AttachCWndHandle(sheet, created_hwnd);
    }
    return sheet.window != nullptr;
}

void PropertySheetBuildPageArray(MfcPropertySheetCompat& sheet) {
    prepare_property_sheet_pages(sheet);
}

int PropertySheetGetPageCount(MfcPropertySheetCompat& sheet) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return static_cast<int>(sheet.pages.size());
    }
    HWND tab = reinterpret_cast<HWND>(
        SendMessageA(sheet.window, PSM_GETTABCONTROL, 0, 0));
    return tab == nullptr ? 0 : TabCtrl_GetItemCount(tab);
}

int PropertySheetGetActiveIndex(MfcPropertySheetCompat& sheet) {
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return static_cast<int>(sheet.header.nStartPage);
    }
    HWND tab = reinterpret_cast<HWND>(
        SendMessageA(sheet.window, PSM_GETTABCONTROL, 0, 0));
    return tab == nullptr ? -1 : TabCtrl_GetCurSel(tab);
}

bool PropertySheetSetActiveIndex(MfcPropertySheetCompat& sheet, int index) {
    if (index < 0) {
        return false;
    }
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        sheet.header.nStartPage = static_cast<UINT>(index);
        return true;
    }
    return SendMessageA(sheet.window, PSM_SETCURSEL,
        static_cast<WPARAM>(index), 0) != 0;
}

void PropertySheetSetActivePage(MfcPropertySheetCompat& sheet,
    MfcPropertyPageCompat* page) {
    if (page == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < sheet.pages.size(); ++index) {
        if (sheet.pages[index] == page) {
            PropertySheetSetActiveIndex(sheet, static_cast<int>(index));
            return;
        }
    }
}

void PropertySheetAddPage(MfcPropertySheetCompat& sheet,
    MfcPropertyPageCompat* page) {
    if (page == nullptr) {
        ThrowMfcMemoryException();
    }
    sheet.pages.push_back(page);
    if (sheet.window == nullptr || !IsWindow(sheet.window)) {
        return;
    }
    PROPSHEETPAGEA sheet_page = page->page;
    PropertyPagePrepareSheetPageTemplate(*page, sheet_page,
        sheet.header.dwFlags & 0x2020U);
    HPROPSHEETPAGE handle = CreatePropertySheetPageA(&sheet_page);
    if (handle == nullptr) {
        ThrowMfcMemoryException();
    }
    if (SendMessageA(sheet.window, PSM_ADDPAGE, 0,
            reinterpret_cast<LPARAM>(handle)) == 0) {
        DestroyPropertySheetPage(handle);
        ThrowMfcMemoryException();
    }
}

void PropertySheetRemovePage(MfcPropertySheetCompat& sheet,
    MfcPropertyPageCompat* page) {
    if (page == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < sheet.pages.size(); ++index) {
        if (sheet.pages[index] == page) {
            PropertySheetRemovePageAt(sheet, static_cast<int>(index));
            return;
        }
    }
}

void PropertySheetRemovePageAt(MfcPropertySheetCompat& sheet, int index) {
    if (index < 0 || index >= static_cast<int>(sheet.pages.size())) {
        return;
    }
    if (sheet.window != nullptr && IsWindow(sheet.window)) {
        SendMessageA(sheet.window, PSM_REMOVEPAGE,
            static_cast<WPARAM>(index), 0);
    }
    sheet.pages.erase(sheet.pages.begin() + index);
}

LRESULT PropertySheetOnInitDialog(MfcPropertySheetCompat& sheet) {
    LRESULT result = sheet.window == nullptr ? 0 :
        DefWindowProcA(sheet.window, WM_INITDIALOG, 0, 0);
    if (sheet.window != nullptr &&
        (CWndGetStyle(sheet) & WS_CHILD) == 0) {
        CWndCenterWindow(sheet, nullptr);
    }
    return result;
}

LRESULT PropertySheetHandleInitDialog(MfcPropertySheetCompat& sheet) {
    return PropertySheetOnInitDialog(sheet);
}

bool PropertySheetOnCommand(MfcPropertySheetCompat& sheet, WPARAM wparam,
    HWND control) {
    if (CWndOnCommand(sheet, wparam, control)) {
        return true;
    }
    if (control != nullptr && HIWORD(wparam) == 0) {
        const UINT dlg_code = static_cast<UINT>(
            SendMessageA(control, WM_GETDLGCODE, 0, 0));
        const LONG style = GetWindowLongA(control, GWL_STYLE) & 0xf;
        if ((dlg_code & (DLGC_BUTTON | DLGC_DEFPUSHBUTTON)) != 0 &&
            (style == BS_PUSHBUTTON || style == BS_DEFPUSHBUTTON ||
                style == BS_OWNERDRAW || style == BS_AUTORADIOBUTTON)) {
            sheet.pressed_button = LOWORD(wparam);
        }
    }
    return false;
}

LRESULT PropertySheetOnCtlColor(MfcPropertySheetCompat& sheet, HDC dc,
    HWND child, UINT type) {
    return CWndOnCtlColor(sheet, dc, child, type);
}

void PropertySheetAssertValid(const MfcPropertySheetCompat& sheet) {
    CWndAssertValid(sheet);
    if (sheet.header.dwSize != sizeof(PROPSHEETHEADERA)) {
        AfxTraceOutput("CPropertySheet has an invalid PROPSHEETHEADER size.\n");
    }
    if ((sheet.header.dwFlags & PSH_PROPSHEETPAGE) == 0) {
        AfxTraceOutput("CPropertySheet missing PSH_PROPSHEETPAGE.\n");
    }
}

void PropertySheetDump(const MfcPropertySheetCompat& sheet) {
    CWndDump(sheet);
    AfxTraceOutput("m_strCaption = %s\n", sheet.caption.c_str());
    AfxTraceOutput("Number of Pages = %zu\n", sheet.pages.size());
    AfxTraceOutput("Stacked = %d\n", sheet.stacked ? 1 : 0);
    AfxTraceOutput("Modeless = %d\n", sheet.modeless ? 1 : 0);
}

MfcRuntimeClassCompat* GetPropertyPageExRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CPropertyPageEx", static_cast<int>(sizeof(MfcPropertyPageExCompat)),
        0xffff,
        +[]() -> void* {
            auto* page = new MfcPropertyPageExCompat();
            ConstructPropertyPageExDefault(*page);
            return page;
        },
        GetPropertyPageRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcPropertyPageExCompat& ConstructPropertyPageExWithTemplateId(
    MfcPropertyPageExCompat& page, UINT template_id, UINT caption_id,
    UINT header_title_id, UINT header_subtitle_id) {
    ConstructPropertyPageExDefault(page);
    PropertyPageExConstructByTemplateId(page, template_id, caption_id,
        header_title_id, header_subtitle_id);
    return page;
}

MfcPropertyPageExCompat& ConstructPropertyPageExWithTemplateName(
    MfcPropertyPageExCompat& page, LPCSTR template_name, UINT caption_id,
    UINT header_title_id, UINT header_subtitle_id) {
    ConstructPropertyPageExDefault(page);
    PropertyPageExConstructByTemplateName(page, template_name, caption_id,
        header_title_id, header_subtitle_id);
    return page;
}

void PropertyPageExConstructByTemplateId(MfcPropertyPageExCompat& page,
    UINT template_id, UINT caption_id, UINT header_title_id,
    UINT header_subtitle_id) {
    PropertyPageExCommonConstruct(page, property_template_from_id(template_id),
        caption_id, header_title_id, header_subtitle_id);
}

void PropertyPageExConstructByTemplateName(MfcPropertyPageExCompat& page,
    LPCSTR template_name, UINT caption_id, UINT header_title_id,
    UINT header_subtitle_id) {
    PropertyPageExCommonConstruct(page, template_name, caption_id,
        header_title_id, header_subtitle_id);
}

MfcPropertyPageExCompat& ConstructPropertyPageExDefault(
    MfcPropertyPageExCompat& page) {
    ConstructPropertyPageDefault(page);
    page.runtime_class = GetPropertyPageExRuntimeClass();
    PropertyPageExCommonConstruct(page, nullptr, 0, 0, 0);
    return page;
}

void PropertyPageExCommonConstruct(MfcPropertyPageExCompat& page,
    LPCSTR template_name, UINT caption_id, UINT header_title_id,
    UINT header_subtitle_id) {
    PropertyPageCommonConstruct(page, template_name, caption_id);
    page.runtime_class = GetPropertyPageExRuntimeClass();
    if (header_title_id != 0) {
        MfcCStringCompat text;
        if (CStringLoadString(text, header_title_id)) {
            page.header_title = text.text;
            page.page.pszHeaderTitle = page.header_title.c_str();
            page.page.dwFlags |= PSP_USEHEADERTITLE;
        }
    }
    if (header_subtitle_id != 0) {
        MfcCStringCompat text;
        if (CStringLoadString(text, header_subtitle_id)) {
            page.header_subtitle = text.text;
            page.page.pszHeaderSubTitle = page.header_subtitle.c_str();
            page.page.dwFlags |= PSP_USEHEADERSUBTITLE;
        }
    }
}

void PropertyPageExAssertValid(const MfcPropertyPageExCompat& page) {
    PropertyPageAssertValid(page);
}

void PropertyPageExDump(const MfcPropertyPageExCompat& page) {
    PropertyPageDump(page);
    AfxTraceOutput("m_strHeaderTitle = %s\n", page.header_title.c_str());
    AfxTraceOutput("m_strHeaderSubTitle = %s\n",
        page.header_subtitle.c_str());
}

void DestroyPropertyPageEx(MfcPropertyPageExCompat& page) {
    DestroyPropertyPage(page);
}

MfcRuntimeClassCompat* GetPropertySheetExRuntimeClass() {
    static MfcRuntimeClassCompat runtime_class{
        "CPropertySheetEx",
        static_cast<int>(sizeof(MfcPropertySheetExCompat)), 0xffff,
        +[]() -> void* {
            auto* sheet = new MfcPropertySheetExCompat();
            ConstructPropertySheetExDefault(*sheet);
            return sheet;
        },
        GetPropertySheetRuntimeClass(), nullptr};
    return &runtime_class;
}

MfcPropertySheetExCompat& ConstructPropertySheetExDefault(
    MfcPropertySheetExCompat& sheet) {
    ConstructPropertySheetDefault(sheet);
    sheet.runtime_class = GetPropertySheetExRuntimeClass();
    PropertySheetExCommonConstruct(sheet, nullptr, 0, nullptr, nullptr,
        nullptr);
    return sheet;
}

MfcPropertySheetExCompat& ConstructPropertySheetExWithCaptionId(
    MfcPropertySheetExCompat& sheet, UINT caption_id, MfcCWndCompat* parent,
    UINT selected_page, HBITMAP watermark, HPALETTE palette, HBITMAP header) {
    ConstructPropertySheetExDefault(sheet);
    PropertySheetExConstructByCaptionId(sheet, caption_id, parent,
        selected_page, watermark, palette, header);
    return sheet;
}

MfcPropertySheetExCompat& ConstructPropertySheetExWithCaptionName(
    MfcPropertySheetExCompat& sheet, const char* caption,
    MfcCWndCompat* parent, UINT selected_page, HBITMAP watermark,
    HPALETTE palette, HBITMAP header) {
    ConstructPropertySheetExDefault(sheet);
    PropertySheetExConstructByCaptionName(sheet, caption, parent,
        selected_page, watermark, palette, header);
    return sheet;
}

void PropertySheetExConstructByCaptionId(MfcPropertySheetExCompat& sheet,
    UINT caption_id, MfcCWndCompat* parent, UINT selected_page,
    HBITMAP watermark, HPALETTE palette, HBITMAP header) {
    MfcCStringCompat text;
    if (CStringLoadString(text, caption_id)) {
        sheet.caption = text.text;
    }
    PropertySheetExCommonConstruct(sheet, parent, selected_page, watermark,
        palette, header);
}

void PropertySheetExConstructByCaptionName(MfcPropertySheetExCompat& sheet,
    const char* caption, MfcCWndCompat* parent, UINT selected_page,
    HBITMAP watermark, HPALETTE palette, HBITMAP header) {
    sheet.caption = caption == nullptr ? "" : caption;
    PropertySheetExCommonConstruct(sheet, parent, selected_page, watermark,
        palette, header);
}

void PropertySheetExCommonConstruct(MfcPropertySheetExCompat& sheet,
    MfcCWndCompat* parent, UINT selected_page, HBITMAP watermark,
    HPALETTE palette, HBITMAP header) {
    PropertySheetCommonConstruct(sheet, parent, selected_page);
    sheet.runtime_class = GetPropertySheetExRuntimeClass();
    sheet.header.dwSize = sizeof(PROPSHEETHEADERA);
    sheet.watermark_size = SIZE{};
    if (watermark != nullptr) {
        sheet.header.hbmWatermark = watermark;
        sheet.header.dwFlags |= PSH_USEHBMWATERMARK;
        BITMAP bitmap{};
        if (GetObjectA(watermark, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
            sheet.watermark_size = SIZE{bitmap.bmWidth, bitmap.bmHeight};
        }
    }
    if (palette != nullptr) {
        sheet.header.hplWatermark = palette;
        sheet.header.dwFlags |= PSH_USEHPLWATERMARK;
    }
    if (header != nullptr) {
        sheet.header.hbmHeader = header;
        sheet.header.dwFlags |= PSH_USEHBMHEADER;
    }
}

void PropertySheetExSetWizardMode(MfcPropertySheetExCompat& sheet) {
    sheet.header.dwFlags |= PSH_WIZARD;
}

SIZE PropertySheetExGetWatermarkSize(const MfcPropertySheetExCompat& sheet) {
    return SizeConstructXY(sheet.watermark_size.cx, sheet.watermark_size.cy);
}

void PropertySheetExBuildPageArray(MfcPropertySheetExCompat& sheet) {
    prepare_property_sheet_pages(sheet);
}

void DestroyPropertySheetEx(MfcPropertySheetExCompat& sheet) {
    DestroyPropertySheet(sheet);
}

void PropertySheetExAddPage(MfcPropertySheetExCompat& sheet,
    MfcPropertyPageExCompat* page) {
    PropertySheetAddPage(sheet, page);
}

void PropertySheetExAssertValid(const MfcPropertySheetExCompat& sheet) {
    PropertySheetAssertValid(sheet);
}

void PropertySheetExDump(const MfcPropertySheetExCompat& sheet) {
    PropertySheetDump(sheet);
}

MfcPropertyPageCompat* DeletePropertyPageScalarDtor(
    MfcPropertyPageCompat* page, unsigned flags) {
    if (page == nullptr) {
        return nullptr;
    }
    DestroyPropertyPage(*page);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(page);
    }
    return page;
}

MfcPropertySheetCompat* DeletePropertySheetScalarDtor(
    MfcPropertySheetCompat* sheet, unsigned flags) {
    if (sheet == nullptr) {
        return nullptr;
    }
    DestroyPropertySheet(*sheet);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(sheet);
    }
    return sheet;
}

MfcPropertyPageExCompat* DeletePropertyPageExScalarDtor(
    MfcPropertyPageExCompat* page, unsigned flags) {
    if (page == nullptr) {
        return nullptr;
    }
    DestroyPropertyPageEx(*page);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(page);
    }
    return page;
}

MfcPropertySheetExCompat* DeletePropertySheetExScalarDtor(
    MfcPropertySheetExCompat* sheet, unsigned flags) {
    if (sheet == nullptr) {
        return nullptr;
    }
    DestroyPropertySheetEx(*sheet);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(sheet);
    }
    return sheet;
}

namespace {

void ArchiveWriteByte(MfcArchiveCompat& archive, unsigned value) {
    const unsigned char byte = static_cast<unsigned char>(value & 0xffU);
    ArchiveWrite(archive, &byte, 1);
}

void ArchiveWriteWord(MfcArchiveCompat& archive, unsigned value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8) & 0xffU),
    };
    ArchiveWrite(archive, bytes, sizeof(bytes));
}

void ArchiveWriteDword(MfcArchiveCompat& archive, unsigned value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8) & 0xffU),
        static_cast<unsigned char>((value >> 16) & 0xffU),
        static_cast<unsigned char>((value >> 24) & 0xffU),
    };
    ArchiveWrite(archive, bytes, sizeof(bytes));
}

unsigned ArchiveReadExact(MfcArchiveCompat& archive, void* buffer,
    unsigned bytes) {
    const unsigned read = ArchiveRead(archive, buffer, bytes);
    if (read != bytes) {
        ThrowArchiveException(3, "end of file");
    }
    return read;
}

unsigned ArchiveReadByteValue(MfcArchiveCompat& archive) {
    unsigned char value = 0;
    ArchiveReadExact(archive, &value, sizeof(value));
    return value;
}

unsigned ArchiveReadWordValue(MfcArchiveCompat& archive) {
    unsigned char bytes[2]{};
    ArchiveReadExact(archive, bytes, sizeof(bytes));
    return static_cast<unsigned>(bytes[0]) |
        (static_cast<unsigned>(bytes[1]) << 8);
}

unsigned ArchiveReadDwordValue(MfcArchiveCompat& archive) {
    unsigned char bytes[4]{};
    ArchiveReadExact(archive, bytes, sizeof(bytes));
    return static_cast<unsigned>(bytes[0]) |
        (static_cast<unsigned>(bytes[1]) << 8) |
        (static_cast<unsigned>(bytes[2]) << 16) |
        (static_cast<unsigned>(bytes[3]) << 24);
}

const char* ArchiveCauseName(unsigned cause) {
    static constexpr const char* kNames[] = {
        "none", "generic", "read only", "end of file", "write only",
        "bad index", "bad class", "bad schema",
    };
    return cause < std::size(kNames) ? kNames[cause] : "unknown";
}

} // namespace

bool ArchiveIsStoring(const MfcArchiveCompat& archive) {
    return archive.storing;
}

bool ArchiveIsLoading(const MfcArchiveCompat& archive) {
    return archive.loading;
}

bool ArchiveIsByteSwapping(const MfcArchiveCompat& archive) {
    return true;
}

bool ArchiveIsBufferEmpty(const MfcArchiveCompat& archive) {
    return archive.position >= archive.buffer.size();
}

unsigned ArchiveGetObjectSchema(const MfcArchiveCompat& archive) {
    return archive.object_schema;
}

void ArchiveSetObjectSchema(MfcArchiveCompat& archive, unsigned schema) {
    archive.object_schema = schema;
}

void ArchiveSetLoadParams(MfcArchiveCompat& archive, unsigned grow_by,
    unsigned hash_size) {
    if (!ArchiveIsLoading(archive)) {
        AfxTraceOutput("afx.inl: SetLoadParams called on storing archive.\n");
    }
    archive.loaded_objects.reserve(grow_by);
    archive.loaded_schemas.reserve(hash_size);
}

void ArchiveSetStoreParams(MfcArchiveCompat& archive, unsigned hash_size) {
    if (!ArchiveIsStoring(archive)) {
        AfxTraceOutput("afx.inl: SetStoreParams called on loading archive.\n");
    }
    archive.stored_object_tags.reserve(hash_size);
    archive.stored_class_tags.reserve(hash_size);
}

MfcArchiveCompat& ArchiveWriteIntInline(MfcArchiveCompat& archive, int value) {
    return ArchiveWriteDWordInline(archive, static_cast<unsigned>(value));
}

MfcArchiveCompat& ArchiveWriteUIntInline(MfcArchiveCompat& archive,
    unsigned value) {
    return ArchiveWriteDWordInline(archive, value);
}

MfcArchiveCompat& ArchiveWriteShortInline(MfcArchiveCompat& archive,
    short value) {
    return ArchiveWriteWordInline(archive, static_cast<unsigned short>(value));
}

MfcArchiveCompat& ArchiveWriteCharInline(MfcArchiveCompat& archive,
    char value) {
    return ArchiveWriteByteInline(archive, static_cast<unsigned char>(value));
}

MfcArchiveCompat& ArchiveWriteByteInline(MfcArchiveCompat& archive,
    unsigned char value) {
    ArchiveWriteByte(archive, value);
    return archive;
}

MfcArchiveCompat& ArchiveWriteWordInline(MfcArchiveCompat& archive,
    unsigned short value) {
    ArchiveWriteWord(archive, value);
    return archive;
}

MfcArchiveCompat& ArchiveWriteDWordInline(MfcArchiveCompat& archive,
    unsigned value) {
    ArchiveWriteDword(archive, value);
    return archive;
}

MfcArchiveCompat& ArchiveWriteLongInline(MfcArchiveCompat& archive,
    long value) {
    ArchiveWriteDword(archive, static_cast<unsigned>(value));
    return archive;
}

MfcArchiveCompat& ArchiveWriteFloatInline(MfcArchiveCompat& archive,
    float value) {
    static_assert(sizeof(float) == sizeof(unsigned));
    unsigned bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    ArchiveWriteDword(archive, bits);
    return archive;
}

MfcArchiveCompat& ArchiveWriteDoubleInline(MfcArchiveCompat& archive,
    double value) {
    ArchiveWrite(archive, &value, sizeof(value));
    return archive;
}

MfcArchiveCompat& ArchiveReadIntInline(MfcArchiveCompat& archive, int& value) {
    unsigned bits = 0;
    ArchiveReadDWordInline(archive, bits);
    value = static_cast<int>(bits);
    return archive;
}

MfcArchiveCompat& ArchiveReadUIntInline(MfcArchiveCompat& archive,
    unsigned& value) {
    return ArchiveReadDWordInline(archive, value);
}

MfcArchiveCompat& ArchiveReadShortInline(MfcArchiveCompat& archive,
    short& value) {
    unsigned short bits = 0;
    ArchiveReadWordInline(archive, bits);
    value = static_cast<short>(bits);
    return archive;
}

MfcArchiveCompat& ArchiveReadCharInline(MfcArchiveCompat& archive,
    char& value) {
    unsigned char bits = 0;
    ArchiveReadByteInline(archive, bits);
    value = static_cast<char>(bits);
    return archive;
}

MfcArchiveCompat& ArchiveReadByteInline(MfcArchiveCompat& archive,
    unsigned char& value) {
    value = static_cast<unsigned char>(ArchiveReadByteValue(archive));
    return archive;
}

MfcArchiveCompat& ArchiveReadWordInline(MfcArchiveCompat& archive,
    unsigned short& value) {
    value = static_cast<unsigned short>(ArchiveReadWordValue(archive));
    return archive;
}

MfcArchiveCompat& ArchiveReadDWordInline(MfcArchiveCompat& archive,
    unsigned& value) {
    value = ArchiveReadDwordValue(archive);
    return archive;
}

MfcArchiveCompat& ArchiveReadLongInline(MfcArchiveCompat& archive,
    long& value) {
    unsigned bits = 0;
    ArchiveReadDWordInline(archive, bits);
    value = static_cast<long>(bits);
    return archive;
}

MfcArchiveCompat& ArchiveReadDoubleInline(MfcArchiveCompat& archive,
    double& value) {
    ArchiveReadExact(archive, &value, sizeof(value));
    return archive;
}

MfcArchiveCompat& ArchiveReadFloatInline(MfcArchiveCompat& archive,
    float& value) {
    unsigned bits = 0;
    ArchiveReadDWordInline(archive, bits);
    std::memcpy(&value, &bits, sizeof(value));
    return archive;
}

MfcCStringCompat& ArchiveConstructEmptyCString(MfcCStringCompat& text) {
    return ConstructCStringEmptyAndReturn(text);
}

void ArchiveNoopInline() {}

MfcArchiveCompat& ArchiveReadObjectPointerInline(MfcArchiveCompat& archive,
    MfcObjectCompat*& object) {
    object = ArchiveReadObject(archive, nullptr);
    return archive;
}

[[noreturn]] void ThrowArchiveException(unsigned cause, const char* detail) {
    const char* cause_name = ArchiveCauseName(cause);
    AfxTraceOutput("CArchive exception: %s%s%s\n", cause_name,
        detail == nullptr ? "" : ": ", detail == nullptr ? "" : detail);
    throw std::runtime_error(cause_name);
}

MfcArchiveCompat& ConstructArchive(MfcArchiveCompat& archive,
    MfcFileCompat* file, unsigned mode, int buffer_size, void* buffer_start) {
    archive.runtime_class = GetCObjectRuntimeClass();
    archive.file = file;
    archive.storing = (mode & 1U) != 0U;
    archive.loading = !archive.storing;
    archive.no_flush_on_delete = (mode & 2U) != 0U;
    archive.open = true;
    archive.buffer_size = buffer_size < 128
        ? 128U : static_cast<unsigned>(buffer_size);
    archive.position = 0;
    archive.object_schema = 0xffffffffU;
    archive.next_object_tag = 1;
    archive.stored_object_tags.clear();
    archive.loaded_objects.clear();
    archive.stored_class_tags.clear();
    archive.loaded_classes.clear();
    archive.loaded_schemas.clear();
    archive.buffer.clear();
    archive.buffer.reserve(archive.buffer_size);
    if (buffer_start != nullptr && archive.loading) {
        archive.buffer.resize(archive.buffer_size);
        std::memcpy(archive.buffer.data(), buffer_start, archive.buffer_size);
    }
    return archive;
}

void DestroyArchive(MfcArchiveCompat& archive) {
    if (archive.open && !archive.no_flush_on_delete) {
        ArchiveClose(archive);
    }
    ArchiveReleaseBuffers(archive);
    archive.runtime_class = nullptr;
}

void ArchiveClose(MfcArchiveCompat& archive) {
    if (!archive.open) {
        return;
    }
    ArchiveFlush(archive);
    archive.open = false;
}

void ArchiveReleaseBuffers(MfcArchiveCompat& archive) {
    archive.open = false;
    archive.buffer.clear();
    archive.position = 0;
    archive.stored_object_tags.clear();
    archive.loaded_objects.clear();
    archive.stored_class_tags.clear();
    archive.loaded_classes.clear();
    archive.loaded_schemas.clear();
    archive.file = nullptr;
}

unsigned ArchiveRead(MfcArchiveCompat& archive, void* buffer, unsigned bytes) {
    if (bytes == 0) {
        return 0;
    }
    if (!archive.loading) {
        ThrowArchiveException(4, "archive is storing");
    }
    if (buffer == nullptr) {
        ThrowArchiveException(1, "null read buffer");
    }
    if (archive.file != nullptr) {
        return FileRead(*archive.file, buffer, bytes);
    }

    const std::size_t available = archive.position < archive.buffer.size()
        ? archive.buffer.size() - archive.position : 0;
    const std::size_t count = std::min<std::size_t>(bytes, available);
    if (count != 0) {
        std::memcpy(buffer, archive.buffer.data() + archive.position, count);
        archive.position += count;
    }
    return static_cast<unsigned>(count);
}

void ArchiveWrite(MfcArchiveCompat& archive, const void* buffer,
    unsigned bytes) {
    if (bytes == 0) {
        return;
    }
    if (!archive.storing) {
        ThrowArchiveException(2, "archive is loading");
    }
    if (buffer == nullptr) {
        ThrowArchiveException(1, "null write buffer");
    }
    if (archive.file != nullptr) {
        FileWrite(*archive.file, buffer, bytes);
        return;
    }

    const std::size_t end = archive.position + bytes;
    if (end > archive.buffer.size()) {
        archive.buffer.resize(end);
    }
    std::memcpy(archive.buffer.data() + archive.position, buffer, bytes);
    archive.position = end;
}

void ArchiveFlush(MfcArchiveCompat& archive) {
    if (!archive.open) {
        return;
    }
    if (archive.storing && archive.file != nullptr) {
        FileFlush(*archive.file);
    }
}

void ArchiveEnsureReadBuffer(MfcArchiveCompat& archive, unsigned needed) {
    if (needed == 0) {
        ThrowArchiveException(1, "zero read request");
    }
    if (!archive.loading) {
        ThrowArchiveException(4, "archive is storing");
    }
    if (archive.file == nullptr &&
        archive.buffer.size() - std::min(archive.position, archive.buffer.size()) <
            needed) {
        ThrowArchiveException(3, "insufficient memory buffer");
    }
}

void ArchiveWriteCount(MfcArchiveCompat& archive, unsigned count) {
    if (count < 0xffffU) {
        ArchiveWriteWord(archive, count);
    } else {
        ArchiveWriteWord(archive, 0xffffU);
        ArchiveWriteDword(archive, count);
    }
}

unsigned ArchiveReadCount(MfcArchiveCompat& archive) {
    unsigned value = ArchiveReadWordValue(archive);
    if (value == 0xffffU) {
        value = ArchiveReadDwordValue(archive);
    }
    return value;
}

void ArchiveWriteCString(MfcArchiveCompat& archive,
    const MfcCStringCompat& text) {
    const unsigned length = static_cast<unsigned>(text.text.size());
    if (length < 0xffU) {
        ArchiveWriteByte(archive, length);
    } else if (length < 0xfffeU) {
        ArchiveWriteByte(archive, 0xffU);
        ArchiveWriteWord(archive, length);
    } else {
        ArchiveWriteByte(archive, 0xffU);
        ArchiveWriteWord(archive, 0xffffU);
        ArchiveWriteDword(archive, length);
    }
    ArchiveWrite(archive, text.text.data(), length);
}

unsigned ArchiveReadStringLength(MfcArchiveCompat& archive) {
    unsigned length = ArchiveReadByteValue(archive);
    if (length < 0xffU) {
        return length;
    }
    length = ArchiveReadWordValue(archive);
    if (length == 0xfffeU) {
        return 0xffffffffU;
    }
    if (length == 0xffffU) {
        return ArchiveReadDwordValue(archive);
    }
    return length;
}

bool ArchiveReadCString(MfcArchiveCompat& archive, MfcCStringCompat& text) {
    bool unicode = false;
    unsigned length = ArchiveReadStringLength(archive);
    if (length == 0xffffffffU) {
        unicode = true;
        length = ArchiveReadStringLength(archive);
        if (length == 0xffffffffU) {
            ThrowArchiveException(5, "invalid CString length");
        }
    }
    if (length == 0) {
        text.text.clear();
        return true;
    }
    const unsigned bytes = unicode ? length * 2U : length;
    std::vector<unsigned char> data(bytes + (unicode ? 2U : 1U), 0);
    ArchiveReadExact(archive, data.data(), bytes);
    if (unicode) {
        std::wstring wide;
        wide.resize(length);
        std::memcpy(wide.data(), data.data(), bytes);
        AssignCStringWide(text, wide.c_str());
    } else {
        text.text.assign(reinterpret_cast<const char*>(data.data()), length);
    }
    return true;
}

void ArchiveReadCStringArray(MfcArchiveCompat& archive,
    MfcCStringCompat* values, int count) {
    if (values == nullptr && count != 0) {
        ThrowArchiveException(1, "null CString array");
    }
    for (int index = 0; index < count; ++index) {
        ArchiveReadCString(archive, values[index]);
    }
}

MfcRuntimeClassCompat* ArchiveLoadRuntimeClass(MfcArchiveCompat& archive,
    unsigned* schema) {
    const unsigned read_schema = ArchiveReadWordValue(archive);
    if (schema != nullptr) {
        *schema = read_schema;
    }
    const unsigned name_length = ArchiveReadWordValue(archive);
    if (name_length >= 64U) {
        return nullptr;
    }
    std::array<char, 64> name{};
    ArchiveReadExact(archive, name.data(), name_length);
    name[name_length] = '\0';

    for (MfcRuntimeClassCompat* current = GetFirstRuntimeClass();
         current != nullptr; current = current->next_class) {
        if (lstrcmpA(current->class_name, name.data()) == 0) {
            archive.loaded_schemas[current] = read_schema;
            return current;
        }
    }
    AfxTraceOutput("Warning: Cannot load %s from archive.\n", name.data());
    return nullptr;
}

void ArchiveStoreRuntimeClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat& runtime_class) {
    const char* name = runtime_class.class_name == nullptr
        ? "" : runtime_class.class_name;
    const unsigned length = static_cast<unsigned>(lstrlenA(name));
    ArchiveWriteWord(archive, runtime_class.schema);
    ArchiveWriteWord(archive, length);
    ArchiveWrite(archive, name, length);
}

void ArchiveWriteAnsiString(MfcArchiveCompat& archive, const char* text) {
    if (text == nullptr) {
        ThrowArchiveException(1, "null string");
    }
    ArchiveWrite(archive, text, static_cast<unsigned>(lstrlenA(text)));
}

char* ArchiveReadLine(MfcArchiveCompat& archive, char* buffer, int max_chars) {
    if (buffer == nullptr || max_chars == 0) {
        ThrowArchiveException(1, "invalid line buffer");
    }
    const int limit = max_chars < 0 ? -max_chars : max_chars;
    int written = 0;
    try {
        while (written < limit - 1) {
            char ch = 0;
            ArchiveReadExact(archive, &ch, 1);
            buffer[written++] = ch;
            if (ch == '\n') {
                break;
            }
            if (ch == '\r') {
                char next = 0;
                if (ArchiveRead(archive, &next, 1) == 1 && next != '\n' &&
                    archive.file == nullptr && archive.position != 0) {
                    --archive.position;
                }
                buffer[written - 1] = '\n';
                break;
            }
        }
    } catch (const std::runtime_error&) {
        if (written == 0) {
            buffer[0] = '\0';
            return nullptr;
        }
    }
    return ArchiveReadLineComplete(buffer, written);
}

char* ArchiveReadLineComplete(char* buffer, int length) {
    if (buffer != nullptr && length >= 0) {
        buffer[length] = '\0';
    }
    return buffer;
}

bool ArchiveReadStringLine(MfcArchiveCompat& archive, MfcCStringCompat& text) {
    text.text.clear();
    std::array<char, 128> chunk{};
    while (true) {
        char* line = ArchiveReadLine(archive, chunk.data(),
            static_cast<int>(chunk.size()));
        if (line == nullptr) {
            return !text.text.empty();
        }
        text.text += line;
        if (text.text.empty() || text.text.back() == '\n' ||
            std::strlen(line) < chunk.size() - 1) {
            break;
        }
    }
    if (!text.text.empty() && text.text.back() == '\n') {
        text.text.pop_back();
    }
    return true;
}

bool LoadLogFontFromResourceString(UINT resource_id, LOGFONTA& font,
    int pixels_per_inch_y) {
    if (resource_id == 0) {
        return false;
    }
    char text[256]{};
    if (AfxLoadStringCompat(resource_id, text, static_cast<int>(sizeof(text))) ==
        0) {
        return false;
    }
    if (pixels_per_inch_y <= 0) {
        HDC screen = GetDC(nullptr);
        pixels_per_inch_y = screen != nullptr
            ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
        if (screen != nullptr) {
            ReleaseDC(nullptr, screen);
        }
    }
    char* newline = std::strchr(text, '\n');
    if (newline != nullptr) {
        const int point_size = CrtAtoi(newline + 1);
        font.lfHeight = MulDiv(point_size, pixels_per_inch_y, 72);
        *newline = '\0';
    }
    lstrcpynA(font.lfFaceName, text, LF_FACESIZE);
    return true;
}

bool IsComboBoxWithStyle(HWND window, UINT combo_style) {
    if (window == nullptr) {
        return false;
    }
    const LONG style = GetWindowLongA(window, GWL_STYLE);
    if ((static_cast<UINT>(style) & 0xfU) != combo_style) {
        return false;
    }
    char class_name[12]{};
    GetClassNameA(window, class_name, static_cast<int>(sizeof(class_name)));
    return lstrcmpiA(class_name, "combobox") == 0;
}

bool WindowHasClassName(HWND window, const char* class_name) {
    if (window == nullptr || class_name == nullptr || !IsWindow(window)) {
        return false;
    }
    char actual[32]{};
    GetClassNameA(window, actual, static_cast<int>(sizeof(actual)));
    return lstrcmpiA(actual, class_name) == 0;
}

HWND FindVisibleChildWindowAtPoint(HWND parent, LONG x, LONG y) {
    if (parent == nullptr) {
        return nullptr;
    }
    POINT screen_point{x, y};
    ClientToScreen(parent, &screen_point);
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if ((GetDlgCtrlID(child) & 0xffff) == 0xffff) {
            continue;
        }
        if ((GetWindowLongA(child, GWL_STYLE) & WS_VISIBLE) == 0) {
            continue;
        }
        RECT rect{};
        GetWindowRect(child, &rect);
        if (PtInRect(&rect, screen_point)) {
            return child;
        }
    }
    return nullptr;
}

void SetWindowTextIfChanged(HWND window, const char* text) {
    if (window == nullptr) {
        return;
    }
    if (text == nullptr) {
        text = "";
    }
    const unsigned length = static_cast<unsigned>(lstrlenA(text));
    char current[256]{};
    if (length < sizeof(current) &&
        GetWindowTextA(window, current, static_cast<int>(sizeof(current))) ==
            static_cast<int>(length) &&
        lstrcmpA(current, text) == 0) {
        return;
    }
    SetWindowTextA(window, text);
}

void DeleteGdiObjectHandle(HGDIOBJ* handle) {
    if (handle == nullptr) {
        return;
    }
    if (*handle != nullptr) {
        DeleteObject(*handle);
        *handle = nullptr;
    }
}

void CloseFocusedComboBoxDropDown(HWND owner) {
    HWND focus = GetFocus();
    if (focus == nullptr || focus == owner) {
        return;
    }
    HWND combo = focus;
    if (!IsComboBoxWithStyle(combo, CBS_DROPDOWNLIST)) {
        combo = GetParent(focus);
        if (combo == owner || !IsComboBoxWithStyle(combo, CBS_DROPDOWN)) {
            return;
        }
    }
    if (owner != nullptr && (GetWindowLongA(owner, GWL_STYLE) & WS_CHILD) != 0 &&
        GetParent(owner) == GetDesktopWindow()) {
        return;
    }
    SendMessageA(combo, CB_SHOWDROPDOWN, FALSE, 0);
}

void UnlockAndFreeGlobal(HGLOBAL global) {
    if (global == nullptr) {
        return;
    }
    const UINT flags = GlobalFlags(global);
    if (flags == GMEM_INVALID_HANDLE) {
        return;
    }
    UINT lock_count = flags & 0xffU;
    while (lock_count-- != 0U) {
        GlobalUnlock(global);
    }
    GlobalFree(global);
}

int CriticalMemoryNewHandler(int requested_size) {
    AfxTraceOutput("Warning: Critical memory allocation failed.\n");
    if (g_mfc_critical_memory_pool == nullptr) {
        AfxTraceOutput("ERROR: Critical memory allocation failed; no safety pool.\n");
        ThrowMfcMemoryException();
    }

    const std::size_t current_size = CrtMemorySize(g_mfc_critical_memory_pool);
    const std::size_t requested = requested_size < 0
        ? 0U : static_cast<std::size_t>(requested_size);
    if (requested + 4U < current_size) {
        const bool tracking = AfxEnableMemoryTracking(false);
        void* shrunk = CrtExpand(g_mfc_critical_memory_pool,
            current_size - (requested + 4U));
        AfxEnableMemoryTracking(tracking);
        if (shrunk != nullptr) {
            g_mfc_critical_memory_pool = shrunk;
        }
        AfxTraceOutput("Warning: Shrinking safety pool from %zu to %zu.\n",
            current_size, CrtMemorySize(g_mfc_critical_memory_pool));
    } else {
        AfxTraceOutput("Warning: Freeing application safety pool.\n");
        CrtFree(g_mfc_critical_memory_pool);
        g_mfc_critical_memory_pool = nullptr;
    }
    return 1;
}

bool ArchiveExceptionGetErrorMessage(unsigned cause, char* destination,
    int destination_chars, unsigned* help_context) {
    if (destination == nullptr || destination_chars <= 0) {
        return false;
    }
    if (help_context != nullptr) {
        *help_context = cause + 0xf1b0U;
    }
    const char* cause_name = ArchiveCauseName(cause);
    std::snprintf(destination, static_cast<std::size_t>(destination_chars),
        "CArchive exception: %s", cause_name);
    destination[destination_chars - 1] = '\0';
    return true;
}

void ArchiveCheckObjectTagLimit(MfcArchiveCompat& archive) {
    if (archive.next_object_tag > 0x3ffffffdU) {
        ThrowArchiveException(5, "object tag table exhausted");
    }
}

void ArchiveMapObject(MfcArchiveCompat& archive, MfcObjectCompat* object) {
    if (archive.storing) {
        if (archive.stored_object_tags.empty()) {
            archive.stored_object_tags[nullptr] = 0;
            archive.next_object_tag = 1;
        }
        if (object != nullptr &&
            archive.stored_object_tags.find(object) ==
                archive.stored_object_tags.end()) {
            ArchiveCheckObjectTagLimit(archive);
            archive.stored_object_tags[object] = archive.next_object_tag++;
        }
    } else {
        if (archive.loaded_objects.empty()) {
            archive.loaded_objects.push_back(nullptr);
            archive.next_object_tag = 1;
        }
        if (object != nullptr) {
            ArchiveCheckObjectTagLimit(archive);
            const unsigned tag = archive.next_object_tag++;
            if (archive.loaded_objects.size() <= tag) {
                archive.loaded_objects.resize(tag + 1, nullptr);
            }
            archive.loaded_objects[tag] = object;
        }
    }
}

void ArchiveWriteObject(MfcArchiveCompat& archive, MfcObjectCompat* object) {
    if (!archive.storing) {
        ThrowArchiveException(2, "archive is loading");
    }
    ArchiveMapObject(archive, nullptr);
    if (object == nullptr) {
        ArchiveWriteWord(archive, 0);
        return;
    }

    auto found = archive.stored_object_tags.find(object);
    if (found == archive.stored_object_tags.end()) {
        if (object->runtime_class == nullptr) {
            ThrowArchiveException(6, "object has no runtime class");
        }
        ArchiveWriteClass(archive, *object->runtime_class);
        ArchiveCheckObjectTagLimit(archive);
        archive.stored_object_tags[object] = archive.next_object_tag++;
        return;
    }

    const unsigned tag = found->second;
    if (tag < 0x7fffU) {
        ArchiveWriteWord(archive, tag);
    } else {
        ArchiveWriteWord(archive, 0x7fffU);
        ArchiveWriteDword(archive, tag);
    }
}

MfcObjectCompat* ArchiveReadObject(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat* expected_class) {
    if (!archive.loading) {
        ThrowArchiveException(4, "archive is storing");
    }
    unsigned schema = 0;
    unsigned object_tag = 0;
    MfcRuntimeClassCompat* runtime_class = ArchiveReadClass(archive,
        expected_class, &schema, &object_tag);
    if (runtime_class == nullptr) {
        if (object_tag == 0) {
            return nullptr;
        }
        if (object_tag >= archive.loaded_objects.size()) {
            ThrowArchiveException(5, "bad object reference");
        }
        auto* object = static_cast<MfcObjectCompat*>(
            archive.loaded_objects[object_tag]);
        if (object != nullptr && expected_class != nullptr &&
            !ObjectIsKindOfRuntimeClass(object, expected_class)) {
            ThrowArchiveException(6, "object class mismatch");
        }
        return object;
    }

    void* created = RuntimeClassCreateObject(*runtime_class);
    if (created == nullptr) {
        ThrowMfcMemoryException();
    }
    auto* object = static_cast<MfcObjectCompat*>(created);
    if (object->runtime_class == nullptr) {
        object->runtime_class = runtime_class;
    }
    ArchiveMapObject(archive, object);
    archive.object_schema = schema;
    AfxAssertValidObject(object, "arcobj.cpp", 0xa3);
    return object;
}

unsigned ArchiveResetObjectSchema(MfcArchiveCompat& archive) {
    const unsigned previous = archive.object_schema;
    archive.object_schema = 0xffffffffU;
    return previous;
}

void ArchiveWriteClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat& runtime_class) {
    if (!archive.storing) {
        ThrowArchiveException(2, "archive is loading");
    }
    if (runtime_class.schema == 0xffffU) {
        AfxTraceOutput("Warning: Cannot call WriteClass for %s.\n",
            runtime_class.class_name);
        ThrowMfcResourceException();
    }
    if (archive.stored_class_tags.empty()) {
        archive.stored_class_tags[nullptr] = 0;
        archive.next_object_tag = std::max(archive.next_object_tag, 1U);
    }

    auto found = archive.stored_class_tags.find(&runtime_class);
    if (found == archive.stored_class_tags.end()) {
        ArchiveWriteWord(archive, 0xffffU);
        ArchiveStoreRuntimeClass(archive, runtime_class);
        ArchiveCheckObjectTagLimit(archive);
        archive.stored_class_tags[&runtime_class] = archive.next_object_tag++;
        return;
    }

    const unsigned tag = found->second | 0x80000000U;
    if ((tag & 0x7fffffffU) < 0x7fffU) {
        ArchiveWriteWord(archive, (tag & 0x7fffU) | 0x8000U);
    } else {
        ArchiveWriteWord(archive, 0x7fffU);
        ArchiveWriteDword(archive, tag);
    }
}

MfcRuntimeClassCompat* ArchiveReadClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat* expected_class, unsigned* schema,
    unsigned* object_tag) {
    if (!archive.loading) {
        ThrowArchiveException(4, "archive is storing");
    }
    ArchiveMapObject(archive, nullptr);

    unsigned word = ArchiveReadWordValue(archive);
    unsigned encoded = 0;
    if (word == 0x7fffU) {
        encoded = ArchiveReadDwordValue(archive);
    } else {
        encoded = ((word & 0x8000U) << 16) | (word & 0x7fffU);
    }

    if ((encoded & 0x80000000U) == 0) {
        if (object_tag == nullptr) {
            ThrowArchiveException(5, "unexpected object tag");
        }
        *object_tag = encoded;
        if (schema != nullptr) {
            *schema = 0;
        }
        return nullptr;
    }

    MfcRuntimeClassCompat* runtime_class = nullptr;
    unsigned loaded_schema = 0;
    if (archive.loaded_classes.empty()) {
        archive.loaded_classes.push_back(nullptr);
    }
    if (word == 0xffffU) {
        runtime_class = ArchiveLoadRuntimeClass(archive, &loaded_schema);
        if (runtime_class == nullptr) {
            ThrowArchiveException(6, "runtime class not found");
        }
        ArchiveCheckObjectTagLimit(archive);
        const unsigned tag = archive.next_object_tag++;
        if (archive.loaded_classes.size() <= tag) {
            archive.loaded_classes.resize(tag + 1, nullptr);
        }
        archive.loaded_classes[tag] = runtime_class;
    } else {
        const unsigned tag = encoded & 0x7fffffffU;
        if (tag == 0 || tag >= archive.loaded_classes.size()) {
            ThrowArchiveException(5, "bad class reference");
        }
        runtime_class = archive.loaded_classes[tag];
        auto schema_found = archive.loaded_schemas.find(runtime_class);
        loaded_schema = schema_found == archive.loaded_schemas.end()
            ? (runtime_class->schema & 0x7fffffffU) : schema_found->second;
    }

    if (expected_class != nullptr &&
        !RuntimeClassIsDerivedFrom(runtime_class, expected_class)) {
        ThrowArchiveException(6, "runtime class mismatch");
    }
    if (schema != nullptr) {
        *schema = loaded_schema;
    } else {
        archive.object_schema = loaded_schema;
    }
    if (object_tag != nullptr) {
        *object_tag = encoded;
    }
    return runtime_class;
}

void CDCAssertValid(const MfcCDCCompat& dc) {
    CObjectAssertValid(&dc);
    if (dc.output_dc != nullptr && GetDeviceCaps(dc.output_dc, TECHNOLOGY) == 0) {
        AfxTraceOutput("CDC has an invalid output HDC %p\n", dc.output_dc);
    }
}

std::unordered_map<HDC, MfcCDCCompat*>* GetTempDCHandleMap(bool create) {
    if (!create && g_dc_handle_map.empty()) {
        return nullptr;
    }
    return &g_dc_handle_map;
}

MfcCDCCompat* CDCFromHandle(HDC handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    auto* map = GetTempDCHandleMap(true);
    auto found = map->find(handle);
    if (found != map->end()) {
        return found->second;
    }
    auto wrapper = std::make_unique<MfcCDCCompat>();
    wrapper->runtime_class = GetCObjectRuntimeClass();
    wrapper->output_dc = handle;
    wrapper->attribute_dc = handle;
    MfcCDCCompat* raw = wrapper.get();
    g_temporary_dcs.push_back(std::move(wrapper));
    (*map)[handle] = raw;
    return raw;
}

static HDC CDCAttributeOrOutput(const MfcCDCCompat& dc) {
    return dc.attribute_dc != nullptr ? dc.attribute_dc : dc.output_dc;
}

HDC CDCGetSafeHdc(const MfcCDCCompat* dc) {
    return dc == nullptr ? nullptr : dc->output_dc;
}

bool CDCAttach(MfcCDCCompat& dc, HDC handle) {
    if (dc.output_dc != nullptr || dc.attribute_dc != nullptr ||
        handle == nullptr) {
        return false;
    }
    dc.runtime_class = GetCObjectRuntimeClass();
    dc.output_dc = handle;
    dc.attribute_dc = handle;
    g_dc_handle_map[handle] = &dc;
    return true;
}

void CDCSetAttribDC(MfcCDCCompat& dc, HDC handle) {
    dc.attribute_dc = handle;
}

void CDCSetOutputDC(MfcCDCCompat& dc, HDC handle) {
    dc.output_dc = handle;
}

void CDCReleaseAttribDC(MfcCDCCompat& dc) {
    dc.attribute_dc = nullptr;
}

void CDCReleaseOutputDC(MfcCDCCompat& dc) {
    if (dc.output_dc != nullptr) {
        g_dc_handle_map.erase(dc.output_dc);
    }
    dc.output_dc = nullptr;
}

MfcCWndCompat* CDCGetWindow(MfcCDCCompat& dc) {
    if (dc.output_dc == nullptr) {
        return nullptr;
    }
    return CWndFromHandle(WindowFromDC(dc.output_dc));
}

bool CDCIsPrinting(const MfcCDCCompat& dc) {
    return dc.printing;
}

bool CDCCreateDC(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data) {
    return CDCAttach(dc, CreateDCA(driver, device, output, init_data));
}

bool CDCCreateDCInline(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data) {
    return CDCCreateDC(dc, driver, device, output, init_data);
}

bool CDCCreateIC(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data) {
    return CDCAttach(dc, CreateICA(driver, device, output, init_data));
}

bool CDCCreateICInline(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data) {
    return CDCCreateIC(dc, driver, device, output, init_data);
}

bool CDCCreateCompatibleDC(MfcCDCCompat& dc, const MfcCDCCompat* source) {
    return CDCAttach(dc, CreateCompatibleDC(CDCGetSafeHdc(source)));
}

int CDCExcludeUpdateRgn(MfcCDCCompat& dc, const MfcCWndCompat& window) {
    return dc.output_dc == nullptr ? ERROR
        : ExcludeUpdateRgn(dc.output_dc, window.window);
}

int CDCExcludeUpdateRgnInline(MfcCDCCompat& dc, const MfcCWndCompat& window) {
    return CDCExcludeUpdateRgn(dc, window);
}

int CDCGetDeviceCaps(const MfcCDCCompat& dc, int index) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetDeviceCaps(handle, index);
}

int CDCGetDeviceCapsInline(const MfcCDCCompat& dc, int index) {
    return CDCGetDeviceCaps(dc, index);
}

POINT CDCGetBrushOrg(const MfcCDCCompat& dc) {
    POINT point{};
    if (dc.output_dc != nullptr) {
        GetBrushOrgEx(dc.output_dc, &point);
    }
    return point;
}

POINT CDCGetBrushOrgInline(const MfcCDCCompat& dc) {
    return CDCGetBrushOrg(dc);
}

POINT CDCSetBrushOrg(MfcCDCCompat& dc, int x, int y) {
    POINT previous{};
    if (dc.output_dc != nullptr) {
        SetBrushOrgEx(dc.output_dc, x, y, &previous);
    }
    return previous;
}

POINT CDCSetBrushOrgXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCSetBrushOrg(dc, x, y);
}

POINT CDCSetBrushOrgPoint(MfcCDCCompat& dc, POINT point) {
    return CDCSetBrushOrg(dc, point.x, point.y);
}

POINT CDCSetBrushOrgPointInline(MfcCDCCompat& dc, POINT point) {
    return CDCSetBrushOrgPoint(dc, point);
}

int CDCEnumObjects(const MfcCDCCompat& dc, int object_type,
    GOBJENUMPROC proc, LPARAM data) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : EnumObjects(handle, object_type, proc, data);
}

int CDCEnumObjectsInline(const MfcCDCCompat& dc, int object_type,
    GOBJENUMPROC proc, LPARAM data) {
    return CDCEnumObjects(dc, object_type, proc, data);
}

DOCINFOA& ConstructDocInfo(DOCINFOA& info) {
    std::memset(&info, 0, sizeof(info));
    return info;
}

int CDCStartDocName(MfcCDCCompat& dc, const char* document_name) {
    DOCINFOA info{};
    info.cbSize = sizeof(info);
    info.lpszDocName = document_name;
    return StartDocA(dc.output_dc, &info);
}

bool MetaFileDCCreate(MfcCDCCompat& dc, const char* file_name) {
    HDC handle = CreateMetaFileA(file_name);
    return CDCAttach(dc, handle);
}

int CDCSaveDC(MfcCDCCompat& dc) {
    if (dc.output_dc == nullptr) {
        return 0;
    }
    int result = 0;
    if (dc.attribute_dc != nullptr) {
        result = SaveDC(dc.attribute_dc);
    }
    if (dc.output_dc != dc.attribute_dc && SaveDC(dc.output_dc) != 0) {
        result = -1;
    }
    return result;
}

BOOL CDCRestoreDC(MfcCDCCompat& dc, int saved_dc) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    BOOL result = TRUE;
    if (dc.output_dc != dc.attribute_dc) {
        result = RestoreDC(dc.output_dc, saved_dc);
    }
    if (dc.attribute_dc != nullptr) {
        result = result != FALSE && RestoreDC(dc.attribute_dc, saved_dc) != FALSE;
    }
    return result;
}

HGDIOBJ CDCSelectObjectRaw(HDC dc, HGDIOBJ object) {
    return dc == nullptr ? nullptr : SelectObject(dc, object);
}

HGDIOBJ CDCSelectObjectBoth(MfcCDCCompat& dc, HGDIOBJ object,
    HGDIOBJ default_result = nullptr) {
    HGDIOBJ previous = default_result;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SelectObject(dc.output_dc, object);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SelectObject(dc.attribute_dc, object);
    }
    return previous;
}

HGDIOBJ CDCSelectStockObject(MfcCDCCompat& dc, int stock_object) {
    HGDIOBJ object = GetStockObject(stock_object);
    if (object == nullptr) {
        return nullptr;
    }
    return CDCSelectObjectBoth(dc, object);
}

HGDIOBJ CDCSelectPen(MfcCDCCompat& dc, HGDIOBJ pen) {
    return CDCSelectObjectBoth(dc, pen);
}

HGDIOBJ CDCSelectPenInline(MfcCDCCompat& dc, HGDIOBJ pen) {
    return CDCSelectPen(dc, pen);
}

HGDIOBJ CDCSelectBrush(MfcCDCCompat& dc, HGDIOBJ brush) {
    return CDCSelectObjectBoth(dc, brush);
}

HGDIOBJ CDCSelectBrushInline(MfcCDCCompat& dc, HGDIOBJ brush) {
    return CDCSelectBrush(dc, brush);
}

HGDIOBJ CDCSelectFont(MfcCDCCompat& dc, HGDIOBJ font) {
    return CDCSelectObjectBoth(dc, font);
}

HGDIOBJ CDCSelectBitmap(MfcCDCCompat& dc, HGDIOBJ bitmap) {
    return CDCSelectObjectBoth(dc, bitmap,
        reinterpret_cast<HGDIOBJ>(INVALID_HANDLE_VALUE));
}

HGDIOBJ CDCSelectGdiObject(MfcCDCCompat& dc, HGDIOBJ object) {
    if (dc.output_dc == nullptr || object == nullptr) {
        return nullptr;
    }
    return SelectObject(dc.output_dc, object);
}

HGDIOBJ CDCSelectGdiObjectInline(MfcCDCCompat& dc, HGDIOBJ object) {
    return CDCSelectGdiObject(dc, object);
}

HPALETTE CDCSelectPalette(MfcCDCCompat& dc, HPALETTE palette,
    BOOL force_background) {
    if (dc.output_dc == nullptr) {
        return nullptr;
    }
    return SelectPalette(dc.output_dc, palette, force_background);
}

COLORREF CDCGetNearestColor(const MfcCDCCompat& dc, COLORREF color) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? CLR_INVALID : GetNearestColor(handle, color);
}

COLORREF CDCGetNearestColorInline(const MfcCDCCompat& dc, COLORREF color) {
    return CDCGetNearestColor(dc, color);
}

UINT CDCRealizePalette(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? GDI_ERROR : RealizePalette(dc.output_dc);
}

UINT CDCRealizePaletteInline(MfcCDCCompat& dc) {
    return CDCRealizePalette(dc);
}

BOOL CDCUpdateColors(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : UpdateColors(dc.output_dc);
}

BOOL CDCUpdateColorsInline(MfcCDCCompat& dc) {
    return CDCUpdateColors(dc);
}

COLORREF CDCGetBkColor(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? CLR_INVALID : GetBkColor(handle);
}

COLORREF CDCGetBkColorInline(const MfcCDCCompat& dc) {
    return CDCGetBkColor(dc);
}

int CDCGetBkMode(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetBkMode(handle);
}

int CDCGetBkModeInline(const MfcCDCCompat& dc) {
    return CDCGetBkMode(dc);
}

int CDCGetPolyFillMode(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetPolyFillMode(handle);
}

int CDCGetPolyFillModeInline(const MfcCDCCompat& dc) {
    return CDCGetPolyFillMode(dc);
}

int CDCGetROP2(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetROP2(handle);
}

int CDCGetROP2Inline(const MfcCDCCompat& dc) {
    return CDCGetROP2(dc);
}

int CDCGetStretchBltMode(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetStretchBltMode(handle);
}

COLORREF CDCGetTextColor(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? CLR_INVALID : GetTextColor(handle);
}

COLORREF CDCGetTextColorInline(const MfcCDCCompat& dc) {
    return CDCGetTextColor(dc);
}

int CDCGetMapMode(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetMapMode(handle);
}

int CDCGetMapModeInline(const MfcCDCCompat& dc) {
    return CDCGetMapMode(dc);
}

COLORREF CDCSetBkColor(MfcCDCCompat& dc, COLORREF color) {
    COLORREF previous = CLR_INVALID;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetBkColor(dc.output_dc, color);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetBkColor(dc.attribute_dc, color);
    }
    return previous;
}

int CDCSetBkMode(MfcCDCCompat& dc, int mode) {
    int previous = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetBkMode(dc.output_dc, mode);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetBkMode(dc.attribute_dc, mode);
    }
    return previous;
}

int CDCSetPolyFillMode(MfcCDCCompat& dc, int mode) {
    int previous = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetPolyFillMode(dc.output_dc, mode);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetPolyFillMode(dc.attribute_dc, mode);
    }
    return previous;
}

int CDCSetROP2(MfcCDCCompat& dc, int mode) {
    int previous = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetROP2(dc.output_dc, mode);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetROP2(dc.attribute_dc, mode);
    }
    return previous;
}

int CDCSetStretchBltMode(MfcCDCCompat& dc, int mode) {
    int previous = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetStretchBltMode(dc.output_dc, mode);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetStretchBltMode(dc.attribute_dc, mode);
    }
    return previous;
}

COLORREF CDCSetTextColor(MfcCDCCompat& dc, COLORREF color) {
    COLORREF previous = CLR_INVALID;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetTextColor(dc.output_dc, color);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetTextColor(dc.attribute_dc, color);
    }
    return previous;
}

int CDCSetMapMode(MfcCDCCompat& dc, int mode) {
    int previous = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetMapMode(dc.output_dc, mode);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetMapMode(dc.attribute_dc, mode);
    }
    return previous;
}

POINT CDCGetViewportOrg(const MfcCDCCompat& dc) {
    POINT point{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetViewportOrgEx(handle, &point);
    }
    return point;
}

POINT CDCGetViewportOrgInline(const MfcCDCCompat& dc) {
    return CDCGetViewportOrg(dc);
}

SIZE CDCGetViewportExt(const MfcCDCCompat& dc) {
    SIZE size{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetViewportExtEx(handle, &size);
    }
    return size;
}

SIZE CDCGetViewportExtInline(const MfcCDCCompat& dc) {
    return CDCGetViewportExt(dc);
}

POINT CDCGetWindowOrg(const MfcCDCCompat& dc) {
    POINT point{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetWindowOrgEx(handle, &point);
    }
    return point;
}

POINT CDCGetWindowOrgInline(const MfcCDCCompat& dc) {
    return CDCGetWindowOrg(dc);
}

SIZE CDCGetWindowExt(const MfcCDCCompat& dc) {
    SIZE size{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetWindowExtEx(handle, &size);
    }
    return size;
}

SIZE CDCGetWindowExtInline(const MfcCDCCompat& dc) {
    return CDCGetWindowExt(dc);
}

using PointDcSetter = BOOL (WINAPI *)(HDC, int, int, LPPOINT);
using SizeDcSetter = BOOL (WINAPI *)(HDC, int, int, LPSIZE);

POINT CDCApplyPointSetter(MfcCDCCompat& dc, PointDcSetter setter,
    int x, int y) {
    POINT previous{};
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        setter(dc.output_dc, x, y, &previous);
    }
    if (dc.attribute_dc != nullptr) {
        setter(dc.attribute_dc, x, y, &previous);
    }
    return previous;
}

SIZE CDCApplySizeSetter(MfcCDCCompat& dc, SizeDcSetter setter, int x, int y) {
    SIZE previous{};
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        setter(dc.output_dc, x, y, &previous);
    }
    if (dc.attribute_dc != nullptr) {
        setter(dc.attribute_dc, x, y, &previous);
    }
    return previous;
}

void CDCSyncAttributePosition(MfcCDCCompat& dc) {
    if (dc.output_dc == nullptr || dc.attribute_dc == nullptr ||
        dc.output_dc == dc.attribute_dc) {
        return;
    }
    POINT current{};
    if (GetCurrentPositionEx(dc.output_dc, &current)) {
        MoveToEx(dc.attribute_dc, current.x, current.y, nullptr);
    }
}

POINT CDCSetViewportOrg(MfcCDCCompat& dc, int x, int y) {
    return CDCApplyPointSetter(dc, SetViewportOrgEx, x, y);
}

POINT CDCSetViewportOrgXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCSetViewportOrg(dc, x, y);
}

POINT CDCSetViewportOrgPoint(MfcCDCCompat& dc, POINT point) {
    return CDCSetViewportOrg(dc, point.x, point.y);
}

POINT CDCOffsetViewportOrg(MfcCDCCompat& dc, int x, int y) {
    return CDCApplyPointSetter(dc, OffsetViewportOrgEx, x, y);
}

POINT CDCOffsetViewportOrgXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCOffsetViewportOrg(dc, x, y);
}

POINT CDCOffsetViewportOrgPoint(MfcCDCCompat& dc, POINT point) {
    return CDCOffsetViewportOrg(dc, point.x, point.y);
}

SIZE CDCSetViewportExt(MfcCDCCompat& dc, int x, int y) {
    return CDCApplySizeSetter(dc, SetViewportExtEx, x, y);
}

SIZE CDCScaleViewportExt(MfcCDCCompat& dc, int x_num, int x_den,
    int y_num, int y_den) {
    SIZE previous{};
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        ScaleViewportExtEx(dc.output_dc, x_num, x_den, y_num, y_den, &previous);
    }
    if (dc.attribute_dc != nullptr) {
        ScaleViewportExtEx(dc.attribute_dc, x_num, x_den, y_num, y_den,
            &previous);
    }
    return previous;
}

POINT CDCSetWindowOrg(MfcCDCCompat& dc, int x, int y) {
    return CDCApplyPointSetter(dc, SetWindowOrgEx, x, y);
}

POINT CDCSetWindowOrgXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCSetWindowOrg(dc, x, y);
}

POINT CDCSetWindowOrgPoint(MfcCDCCompat& dc, POINT point) {
    return CDCSetWindowOrg(dc, point.x, point.y);
}

POINT CDCOffsetWindowOrg(MfcCDCCompat& dc, int x, int y) {
    return CDCApplyPointSetter(dc, OffsetWindowOrgEx, x, y);
}

POINT CDCOffsetWindowOrgXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCOffsetWindowOrg(dc, x, y);
}

POINT CDCOffsetWindowOrgPoint(MfcCDCCompat& dc, POINT point) {
    return CDCOffsetWindowOrg(dc, point.x, point.y);
}

SIZE CDCSetWindowExt(MfcCDCCompat& dc, int x, int y) {
    return CDCApplySizeSetter(dc, SetWindowExtEx, x, y);
}

SIZE CDCScaleWindowExt(MfcCDCCompat& dc, int x_num, int x_den,
    int y_num, int y_den) {
    SIZE previous{};
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        ScaleWindowExtEx(dc.output_dc, x_num, x_den, y_num, y_den, &previous);
    }
    if (dc.attribute_dc != nullptr) {
        ScaleWindowExtEx(dc.attribute_dc, x_num, x_den, y_num, y_den,
            &previous);
    }
    return previous;
}

BOOL CDCDPtoLP(MfcCDCCompat& dc, POINT* points, int count) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE : DPtoLP(handle, points, count);
}

BOOL CDCDPtoLPPointsInline(MfcCDCCompat& dc, POINT* points, int count) {
    return CDCDPtoLP(dc, points, count);
}

BOOL CDCDPtoLPRect(MfcCDCCompat& dc, RECT& rect) {
    return CDCDPtoLP(dc, reinterpret_cast<POINT*>(&rect), 2);
}

BOOL CDCDPtoLPRectInline(MfcCDCCompat& dc, RECT& rect) {
    return CDCDPtoLPRect(dc, rect);
}

BOOL CDCLPtoDP(MfcCDCCompat& dc, POINT* points, int count) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE : LPtoDP(handle, points, count);
}

BOOL CDCLPtoDPPointsInline(MfcCDCCompat& dc, POINT* points, int count) {
    return CDCLPtoDP(dc, points, count);
}

BOOL CDCLPtoDPRect(MfcCDCCompat& dc, RECT& rect) {
    return CDCLPtoDP(dc, reinterpret_cast<POINT*>(&rect), 2);
}

BOOL CDCLPtoDPRectInline(MfcCDCCompat& dc, RECT& rect) {
    return CDCLPtoDPRect(dc, rect);
}

void CDCGetClipBox(MfcCDCCompat& dc, RECT& rect) {
    if (dc.output_dc != nullptr) {
        GetClipBox(dc.output_dc, &rect);
    }
}

int CDCSelectClipRgn(MfcCDCCompat& dc, HRGN region) {
    int result = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = SelectClipRgn(dc.output_dc, region);
    }
    if (dc.attribute_dc != nullptr) {
        result = SelectClipRgn(dc.attribute_dc, region);
    }
    return result;
}

int CDCExcludeClipRect(MfcCDCCompat& dc, int left, int top, int right,
    int bottom) {
    int result = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = ExcludeClipRect(dc.output_dc, left, top, right, bottom);
    }
    if (dc.attribute_dc != nullptr) {
        result = ExcludeClipRect(dc.attribute_dc, left, top, right, bottom);
    }
    return result;
}

int CDCExcludeClipRectIndirect(MfcCDCCompat& dc, const RECT& rect) {
    return CDCExcludeClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
}

int CDCIntersectClipRect(MfcCDCCompat& dc, int left, int top, int right,
    int bottom) {
    int result = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = IntersectClipRect(dc.output_dc, left, top, right, bottom);
    }
    if (dc.attribute_dc != nullptr) {
        result = IntersectClipRect(dc.attribute_dc, left, top, right, bottom);
    }
    return result;
}

int CDCIntersectClipRectIndirect(MfcCDCCompat& dc, const RECT& rect) {
    return CDCIntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
}

int CDCOffsetClipRgn(MfcCDCCompat& dc, int x, int y) {
    int result = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = OffsetClipRgn(dc.output_dc, x, y);
    }
    if (dc.attribute_dc != nullptr) {
        result = OffsetClipRgn(dc.attribute_dc, x, y);
    }
    return result;
}

int CDCOffsetClipRgnPoint(MfcCDCCompat& dc, POINT point) {
    return CDCOffsetClipRgn(dc, point.x, point.y);
}

BOOL CDCFillRgn(MfcCDCCompat& dc, HRGN region, HBRUSH brush) {
    return dc.output_dc == nullptr ? FALSE : FillRgn(dc.output_dc, region, brush);
}

BOOL CDCFillRgnInline(MfcCDCCompat& dc, HRGN region, HBRUSH brush) {
    return CDCFillRgn(dc, region, brush);
}

BOOL CDCFrameRgn(MfcCDCCompat& dc, HRGN region, HBRUSH brush, int width,
    int height) {
    return dc.output_dc == nullptr ? FALSE
        : FrameRgn(dc.output_dc, region, brush, width, height);
}

BOOL CDCFrameRgnInline(MfcCDCCompat& dc, HRGN region, HBRUSH brush, int width,
    int height) {
    return CDCFrameRgn(dc, region, brush, width, height);
}

BOOL CDCInvertRgn(MfcCDCCompat& dc, HRGN region) {
    return dc.output_dc == nullptr ? FALSE : InvertRgn(dc.output_dc, region);
}

BOOL CDCInvertRgnInline(MfcCDCCompat& dc, HRGN region) {
    return CDCInvertRgn(dc, region);
}

BOOL CDCPaintRgn(MfcCDCCompat& dc, HRGN region) {
    return dc.output_dc == nullptr ? FALSE : PaintRgn(dc.output_dc, region);
}

BOOL CDCPaintRgnInline(MfcCDCCompat& dc, HRGN region) {
    return CDCPaintRgn(dc, region);
}

BOOL CDCPtVisible(MfcCDCCompat& dc, int x, int y) {
    return dc.output_dc == nullptr ? FALSE : PtVisible(dc.output_dc, x, y);
}

BOOL CDCPtVisibleXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCPtVisible(dc, x, y);
}

BOOL CDCPtVisiblePoint(MfcCDCCompat& dc, POINT point) {
    return CDCPtVisible(dc, point.x, point.y);
}

BOOL CDCPtVisiblePointInline(MfcCDCCompat& dc, POINT point) {
    return CDCPtVisiblePoint(dc, point);
}

BOOL CDCRectVisible(MfcCDCCompat& dc, const RECT& rect) {
    return dc.output_dc == nullptr ? FALSE : RectVisible(dc.output_dc, &rect);
}

BOOL CDCRectVisibleInline(MfcCDCCompat& dc, const RECT& rect) {
    return CDCRectVisible(dc, rect);
}

POINT CDCGetCurrentPosition(const MfcCDCCompat& dc) {
    POINT point{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetCurrentPositionEx(handle, &point);
    }
    return point;
}

POINT CDCGetCurrentPositionInline(const MfcCDCCompat& dc) {
    return CDCGetCurrentPosition(dc);
}

POINT CDCMoveTo(MfcCDCCompat& dc, int x, int y) {
    return CDCApplyPointSetter(dc, MoveToEx, x, y);
}

POINT CDCMoveToXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCMoveTo(dc, x, y);
}

POINT CDCMoveToPoint(MfcCDCCompat& dc, POINT point) {
    return CDCMoveTo(dc, point.x, point.y);
}

void CDCLineTo(MfcCDCCompat& dc, int x, int y) {
    if (dc.attribute_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        MoveToEx(dc.attribute_dc, x, y, nullptr);
    }
    if (dc.output_dc != nullptr) {
        LineTo(dc.output_dc, x, y);
    }
}

BOOL CDCLineToXYInline(MfcCDCCompat& dc, int x, int y) {
    if (dc.attribute_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        MoveToEx(dc.attribute_dc, x, y, nullptr);
    }
    return dc.output_dc == nullptr ? FALSE : LineTo(dc.output_dc, x, y);
}

BOOL CDCArc(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    return dc.output_dc == nullptr ? FALSE
        : Arc(dc.output_dc, left, top, right, bottom, x_start, y_start,
            x_end, y_end);
}

BOOL CDCArcInline(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    return CDCArc(dc, left, top, right, bottom, x_start, y_start, x_end, y_end);
}

BOOL CDCArcRect(MfcCDCCompat& dc, const RECT& rect, int x_start, int y_start,
    int x_end, int y_end) {
    return CDCArc(dc, rect.left, rect.top, rect.right, rect.bottom, x_start,
        y_start, x_end, y_end);
}

BOOL CDCArcRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCArcRect(dc, rect, x_start, y_start, x_end, y_end);
}

BOOL CDCPolyline(MfcCDCCompat& dc, const POINT* points, int count) {
    return dc.output_dc == nullptr ? FALSE : Polyline(dc.output_dc, points, count);
}

BOOL CDCPolylineInline(MfcCDCCompat& dc, const POINT* points, int count) {
    return CDCPolyline(dc, points, count);
}

int CDCFillRect(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush) {
    return dc.output_dc == nullptr ? 0 : FillRect(dc.output_dc, &rect, brush);
}

int CDCFillRectInline(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush) {
    return CDCFillRect(dc, rect, brush);
}

int CDCFrameRect(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush) {
    return dc.output_dc == nullptr ? 0 : FrameRect(dc.output_dc, &rect, brush);
}

int CDCFrameRectInline(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush) {
    return CDCFrameRect(dc, rect, brush);
}

BOOL CDCInvertRect(MfcCDCCompat& dc, const RECT& rect) {
    return dc.output_dc == nullptr ? FALSE : InvertRect(dc.output_dc, &rect);
}

BOOL CDCInvertRectInline(MfcCDCCompat& dc, const RECT& rect) {
    return CDCInvertRect(dc, rect);
}

BOOL CDCDrawIcon(MfcCDCCompat& dc, int x, int y, HICON icon) {
    return dc.output_dc == nullptr ? FALSE : DrawIcon(dc.output_dc, x, y, icon);
}

BOOL CDCDrawIconXYInline(MfcCDCCompat& dc, int x, int y, HICON icon) {
    return CDCDrawIcon(dc, x, y, icon);
}

BOOL CDCDrawIconPoint(MfcCDCCompat& dc, POINT point, HICON icon) {
    return CDCDrawIcon(dc, point.x, point.y, icon);
}

BOOL CDCDrawIconPointInline(MfcCDCCompat& dc, POINT point, HICON icon) {
    return CDCDrawIconPoint(dc, point, icon);
}

BOOL CDCDrawEdge(MfcCDCCompat& dc, RECT& rect, UINT edge, UINT flags) {
    return dc.output_dc == nullptr ? FALSE : DrawEdge(dc.output_dc, &rect, edge, flags);
}

BOOL CDCDrawEdgeInline(MfcCDCCompat& dc, RECT& rect, UINT edge, UINT flags) {
    return CDCDrawEdge(dc, rect, edge, flags);
}

BOOL CDCDrawFrameControl(MfcCDCCompat& dc, RECT& rect, UINT type, UINT state) {
    return dc.output_dc == nullptr ? FALSE
        : DrawFrameControl(dc.output_dc, &rect, type, state);
}

BOOL CDCDrawFrameControlInline(MfcCDCCompat& dc, RECT& rect, UINT type,
    UINT state) {
    return CDCDrawFrameControl(dc, rect, type, state);
}

static BOOL CDCDrawStateRawInline(MfcCDCCompat& dc, HBRUSH brush,
    DRAWSTATEPROC proc, LPARAM data, WPARAM length, int x, int y, int width,
    int height, UINT flags) {
    return dc.output_dc == nullptr ? FALSE
        : DrawStateA(dc.output_dc, brush, proc, data, length, x, y, width,
            height, flags);
}

BOOL CDCDrawStateIconBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, HBITMAP bitmap, UINT flags, HBRUSH brush) {
    return CDCDrawStateRawInline(dc, brush, nullptr,
        reinterpret_cast<LPARAM>(bitmap), 0, x, y, width, height,
        flags | DST_BITMAP);
}

BOOL CDCDrawStateIconInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const MfcGdiObjectCompat* bitmap, UINT flags) {
    return CDCDrawStateIconBrushInline(dc, x, y, width, height,
        BitmapGetSafeHandle(bitmap), flags, nullptr);
}

BOOL CDCDrawStateTextBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, HICON icon, UINT flags, HBRUSH brush) {
    return CDCDrawStateRawInline(dc, brush, nullptr,
        reinterpret_cast<LPARAM>(icon), 0, x, y, width, height,
        flags | DST_ICON);
}

BOOL CDCDrawStateTextInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, HICON icon, UINT flags) {
    return CDCDrawStateTextBrushInline(dc, x, y, width, height, icon, flags,
        nullptr);
}

BOOL CDCDrawStateBitmapBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const char* text, UINT flags, bool prefix_text,
    UINT text_length, HBRUSH brush) {
    return CDCDrawStateRawInline(dc, brush, nullptr,
        reinterpret_cast<LPARAM>(text), text_length, x, y, width, height,
        flags | (prefix_text ? DST_PREFIXTEXT : DST_TEXT));
}

BOOL CDCDrawStateBitmapInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const char* text, UINT flags, bool prefix_text,
    UINT text_length) {
    return CDCDrawStateBitmapBrushInline(dc, x, y, width, height, text, flags,
        prefix_text, text_length, nullptr);
}

BOOL CDCDrawStateProcBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, DRAWSTATEPROC proc, LPARAM data, UINT flags, HBRUSH brush) {
    return CDCDrawStateRawInline(dc, brush, proc, data, 0, x, y, width,
        height, flags);
}

BOOL CDCDrawStateProcInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, DRAWSTATEPROC proc, LPARAM data, UINT flags) {
    return CDCDrawStateProcBrushInline(dc, x, y, width, height, proc, data,
        flags, nullptr);
}

BOOL CDCChord(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    return dc.output_dc == nullptr ? FALSE
        : Chord(dc.output_dc, left, top, right, bottom, x_start, y_start,
            x_end, y_end);
}

BOOL CDCChordInline(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    return CDCChord(dc, left, top, right, bottom, x_start, y_start, x_end,
        y_end);
}

BOOL CDCChordRect(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCChord(dc, rect.left, rect.top, rect.right, rect.bottom, x_start,
        y_start, x_end, y_end);
}

BOOL CDCChordRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCChordRect(dc, rect, x_start, y_start, x_end, y_end);
}

BOOL CDCDrawFocusRect(MfcCDCCompat& dc, const RECT& rect) {
    return dc.output_dc == nullptr ? FALSE : DrawFocusRect(dc.output_dc, &rect);
}

BOOL CDCDrawFocusRectInline(MfcCDCCompat& dc, const RECT& rect) {
    return CDCDrawFocusRect(dc, rect);
}

BOOL CDCEllipse(MfcCDCCompat& dc, int left, int top, int right, int bottom) {
    return dc.output_dc == nullptr ? FALSE
        : Ellipse(dc.output_dc, left, top, right, bottom);
}

BOOL CDCEllipseInline(MfcCDCCompat& dc, int left, int top, int right,
    int bottom) {
    return CDCEllipse(dc, left, top, right, bottom);
}

BOOL CDCEllipseRect(MfcCDCCompat& dc, const RECT& rect) {
    return CDCEllipse(dc, rect.left, rect.top, rect.right, rect.bottom);
}

BOOL CDCEllipseRectInline(MfcCDCCompat& dc, const RECT& rect) {
    return CDCEllipseRect(dc, rect);
}

BOOL CDCPie(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    return dc.output_dc == nullptr ? FALSE
        : Pie(dc.output_dc, left, top, right, bottom, x_start, y_start,
            x_end, y_end);
}

BOOL CDCPieInline(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    return CDCPie(dc, left, top, right, bottom, x_start, y_start, x_end,
        y_end);
}

BOOL CDCPieRect(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCPie(dc, rect.left, rect.top, rect.right, rect.bottom, x_start,
        y_start, x_end, y_end);
}

BOOL CDCPieRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCPieRect(dc, rect, x_start, y_start, x_end, y_end);
}

BOOL CDCPolygon(MfcCDCCompat& dc, const POINT* points, int count) {
    return dc.output_dc == nullptr ? FALSE : Polygon(dc.output_dc, points, count);
}

BOOL CDCPolygonInline(MfcCDCCompat& dc, const POINT* points, int count) {
    return CDCPolygon(dc, points, count);
}

BOOL CDCPolyPolygon(MfcCDCCompat& dc, const POINT* points,
    const INT* poly_counts, int count) {
    return dc.output_dc == nullptr ? FALSE
        : PolyPolygon(dc.output_dc, points, poly_counts, count);
}

BOOL CDCPolyPolygonInline(MfcCDCCompat& dc, const POINT* points,
    const INT* poly_counts, int count) {
    return CDCPolyPolygon(dc, points, poly_counts, count);
}

BOOL CDCRectangle(MfcCDCCompat& dc, int left, int top, int right,
    int bottom) {
    return dc.output_dc == nullptr ? FALSE
        : Rectangle(dc.output_dc, left, top, right, bottom);
}

BOOL CDCRectangleInline(MfcCDCCompat& dc, int left, int top, int right,
    int bottom) {
    return CDCRectangle(dc, left, top, right, bottom);
}

BOOL CDCRectangleRect(MfcCDCCompat& dc, const RECT& rect) {
    return CDCRectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
}

BOOL CDCRectangleRectInline(MfcCDCCompat& dc, const RECT& rect) {
    return CDCRectangleRect(dc, rect);
}

BOOL CDCRoundRect(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int width, int height) {
    return dc.output_dc == nullptr ? FALSE
        : RoundRect(dc.output_dc, left, top, right, bottom, width, height);
}

BOOL CDCRoundRectInline(MfcCDCCompat& dc, int left, int top, int right,
    int bottom, int width, int height) {
    return CDCRoundRect(dc, left, top, right, bottom, width, height);
}

BOOL CDCRoundRectRect(MfcCDCCompat& dc, const RECT& rect, POINT point) {
    return CDCRoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
        point.x, point.y);
}

BOOL CDCRoundRectRectInline(MfcCDCCompat& dc, const RECT& rect, POINT point) {
    return CDCRoundRectRect(dc, rect, point);
}

BOOL CDCPatBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    DWORD raster_op) {
    return dc.output_dc == nullptr ? FALSE
        : PatBlt(dc.output_dc, x, y, width, height, raster_op);
}

BOOL CDCPatBltInline(MfcCDCCompat& dc, int x, int y, int width, int height,
    DWORD raster_op) {
    return CDCPatBlt(dc, x, y, width, height, raster_op);
}

BOOL CDCBitBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, DWORD raster_op) {
    return dc.output_dc == nullptr ? FALSE
        : BitBlt(dc.output_dc, x, y, width, height,
            CDCGetSafeHdc(&source), src_x, src_y, raster_op);
}

BOOL CDCBitBltInline(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, DWORD raster_op) {
    return CDCBitBlt(dc, x, y, width, height, source, src_x, src_y,
        raster_op);
}

BOOL CDCStretchBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, int src_width,
    int src_height, DWORD raster_op) {
    return dc.output_dc == nullptr ? FALSE
        : StretchBlt(dc.output_dc, x, y, width, height,
            CDCGetSafeHdc(&source), src_x, src_y, src_width, src_height,
            raster_op);
}

BOOL CDCStretchBltInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const MfcCDCCompat& source, int src_x, int src_y,
    int src_width, int src_height, DWORD raster_op) {
    return CDCStretchBlt(dc, x, y, width, height, source, src_x, src_y,
        src_width, src_height, raster_op);
}

COLORREF CDCGetPixel(MfcCDCCompat& dc, int x, int y) {
    return dc.output_dc == nullptr ? CLR_INVALID : GetPixel(dc.output_dc, x, y);
}

COLORREF CDCGetPixelXYInline(MfcCDCCompat& dc, int x, int y) {
    return CDCGetPixel(dc, x, y);
}

COLORREF CDCGetPixelPoint(MfcCDCCompat& dc, POINT point) {
    return CDCGetPixel(dc, point.x, point.y);
}

COLORREF CDCGetPixelPointInline(MfcCDCCompat& dc, POINT point) {
    return CDCGetPixelPoint(dc, point);
}

COLORREF CDCSetPixel(MfcCDCCompat& dc, int x, int y, COLORREF color) {
    return dc.output_dc == nullptr ? CLR_INVALID
        : SetPixel(dc.output_dc, x, y, color);
}

COLORREF CDCSetPixelXYInline(MfcCDCCompat& dc, int x, int y,
    COLORREF color) {
    return CDCSetPixel(dc, x, y, color);
}

COLORREF CDCSetPixelPoint(MfcCDCCompat& dc, POINT point, COLORREF color) {
    return CDCSetPixel(dc, point.x, point.y, color);
}

COLORREF CDCSetPixelPointInline(MfcCDCCompat& dc, POINT point,
    COLORREF color) {
    return CDCSetPixelPoint(dc, point, color);
}

BOOL CDCFloodFill(MfcCDCCompat& dc, int x, int y, COLORREF color) {
    return dc.output_dc == nullptr ? FALSE : FloodFill(dc.output_dc, x, y, color);
}

BOOL CDCFloodFillInline(MfcCDCCompat& dc, int x, int y, COLORREF color) {
    return CDCFloodFill(dc, x, y, color);
}

BOOL CDCExtFloodFill(MfcCDCCompat& dc, int x, int y, COLORREF color,
    UINT fill_type) {
    return dc.output_dc == nullptr ? FALSE
        : ExtFloodFill(dc.output_dc, x, y, color, fill_type);
}

BOOL CDCExtFloodFillInline(MfcCDCCompat& dc, int x, int y, COLORREF color,
    UINT fill_type) {
    return CDCExtFloodFill(dc, x, y, color, fill_type);
}

BOOL CDCTextOutChars(MfcCDCCompat& dc, int x, int y, const char* text,
    int count) {
    return dc.output_dc == nullptr ? FALSE
        : TextOutA(dc.output_dc, x, y, text, count);
}

BOOL CDCTextOutCharsInline(MfcCDCCompat& dc, int x, int y,
    const char* text, int count) {
    return CDCTextOutChars(dc, x, y, text, count);
}

BOOL CDCExtTextOutChars(MfcCDCCompat& dc, int x, int y, UINT options,
    const RECT* rect, const char* text, UINT count, const INT* dx) {
    return dc.output_dc == nullptr ? FALSE
        : ExtTextOutA(dc.output_dc, x, y, options, rect, text, count, dx);
}

BOOL CDCExtTextOutCharsInline(MfcCDCCompat& dc, int x, int y, UINT options,
    const RECT* rect, const char* text, UINT count, const INT* dx) {
    return CDCExtTextOutChars(dc, x, y, options, rect, text, count, dx);
}

SIZE CDCTabbedTextOutChars(MfcCDCCompat& dc, int x, int y,
    const char* text, int count, int tab_count, const INT* tab_positions,
    int tab_origin) {
    LONG packed = 0;
    if (dc.output_dc != nullptr) {
        packed = TabbedTextOutA(dc.output_dc, x, y, text, count, tab_count,
            tab_positions, tab_origin);
    }
    return SizeConstructFromDWord(static_cast<DWORD>(packed));
}

SIZE CDCTabbedTextOutCharsInline(MfcCDCCompat& dc, int x, int y,
    const char* text, int count, int tab_count, const INT* tab_positions,
    int tab_origin) {
    return CDCTabbedTextOutChars(dc, x, y, text, count, tab_count,
        tab_positions, tab_origin);
}

int CDCDrawTextChars(MfcCDCCompat& dc, const char* text, int count,
    RECT& rect, UINT format) {
    return dc.output_dc == nullptr ? 0
        : DrawTextA(dc.output_dc, text, count, &rect, format);
}

int CDCDrawTextCharsInline(MfcCDCCompat& dc, const char* text, int count,
    RECT& rect, UINT format) {
    return CDCDrawTextChars(dc, text, count, rect, format);
}

SIZE CDCGetTextExtentChars(MfcCDCCompat& dc, const char* text, int count) {
    SIZE size{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetTextExtentPoint32A(handle, text, count, &size);
    }
    return size;
}

SIZE CDCGetTextExtentCharsInline(MfcCDCCompat& dc, const char* text,
    int count) {
    return CDCGetTextExtentChars(dc, text, count);
}

SIZE CDCGetOutputTextExtentChars(MfcCDCCompat& dc, const char* text,
    int count) {
    SIZE size{};
    if (dc.output_dc != nullptr) {
        GetTextExtentPoint32A(dc.output_dc, text, count, &size);
    }
    return size;
}

SIZE CDCGetOutputTextExtentCharsInline(MfcCDCCompat& dc, const char* text,
    int count) {
    return CDCGetOutputTextExtentChars(dc, text, count);
}

SIZE CDCGetTabbedTextExtentChars(MfcCDCCompat& dc, const char* text,
    int count, int tab_count, const INT* tab_positions) {
    HDC handle = CDCAttributeOrOutput(dc);
    DWORD packed = 0;
    if (handle != nullptr) {
        packed = GetTabbedTextExtentA(handle, text, count, tab_count,
            tab_positions);
    }
    return SizeConstructFromDWord(packed);
}

SIZE CDCGetTabbedTextExtentCharsInline(MfcCDCCompat& dc, const char* text,
    int count, int tab_count, const INT* tab_positions) {
    return CDCGetTabbedTextExtentChars(dc, text, count, tab_count,
        tab_positions);
}

SIZE CDCGetTabbedTextExtentString(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions) {
    return CDCGetTabbedTextExtentChars(dc, text.text.c_str(),
        static_cast<int>(text.text.size()), tab_count, tab_positions);
}

SIZE CDCGetTabbedTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions) {
    return CDCGetTabbedTextExtentString(dc, text, tab_count, tab_positions);
}

SIZE CDCGetOutputTabbedTextExtentChars(MfcCDCCompat& dc, const char* text,
    int count, int tab_count, const INT* tab_positions) {
    DWORD packed = 0;
    if (dc.output_dc != nullptr) {
        packed = GetTabbedTextExtentA(dc.output_dc, text, count, tab_count,
            tab_positions);
    }
    return SizeConstructFromDWord(packed);
}

SIZE CDCGetOutputTabbedTextExtentCharsInline(MfcCDCCompat& dc,
    const char* text, int count, int tab_count, const INT* tab_positions) {
    return CDCGetOutputTabbedTextExtentChars(dc, text, count, tab_count,
        tab_positions);
}

SIZE CDCGetOutputTabbedTextExtentString(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions) {
    return CDCGetOutputTabbedTextExtentChars(dc, text.text.c_str(),
        static_cast<int>(text.text.size()), tab_count, tab_positions);
}

SIZE CDCGetOutputTabbedTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions) {
    return CDCGetOutputTabbedTextExtentString(dc, text, tab_count,
        tab_positions);
}

BOOL CDCGrayString(MfcCDCCompat& dc, HBRUSH brush, GRAYSTRINGPROC proc,
    LPARAM data, int count, int x, int y, int width, int height) {
    return dc.output_dc == nullptr ? FALSE
        : GrayStringA(dc.output_dc, brush, proc, data, count, x, y, width,
            height);
}

BOOL CDCGrayStringInline(MfcCDCCompat& dc, HBRUSH brush, GRAYSTRINGPROC proc,
    LPARAM data, int count, int x, int y, int width, int height) {
    return CDCGrayString(dc, brush, proc, data, count, x, y, width, height);
}

UINT CDCGetTextAlign(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? GDI_ERROR : GetTextAlign(handle);
}

UINT CDCGetTextAlignInline(const MfcCDCCompat& dc) {
    return CDCGetTextAlign(dc);
}

int CDCGetTextFace(const MfcCDCCompat& dc, int count, char* face_name) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetTextFaceA(handle, count, face_name);
}

int CDCGetTextFaceInline(const MfcCDCCompat& dc, int count, char* face_name) {
    return CDCGetTextFace(dc, count, face_name);
}

std::string CDCGetTextFaceString(const MfcCDCCompat& dc) {
    char face_name[256] = {};
    const int length = CDCGetTextFace(dc, static_cast<int>(sizeof(face_name)),
        face_name);
    return length <= 0 ? std::string{} : std::string(face_name, length);
}

std::string CDCGetTextFaceCStringInline(const MfcCDCCompat& dc) {
    return CDCGetTextFaceString(dc);
}

BOOL CDCGetTextMetrics(const MfcCDCCompat& dc, TEXTMETRICA& metrics) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE : GetTextMetricsA(handle, &metrics);
}

BOOL CDCGetTextMetricsInline(const MfcCDCCompat& dc, TEXTMETRICA& metrics) {
    return CDCGetTextMetrics(dc, metrics);
}

BOOL CDCGetOutputTextMetrics(const MfcCDCCompat& dc, TEXTMETRICA& metrics) {
    return dc.output_dc == nullptr ? FALSE : GetTextMetricsA(dc.output_dc,
        &metrics);
}

BOOL CDCGetOutputTextMetricsInline(const MfcCDCCompat& dc,
    TEXTMETRICA& metrics) {
    return CDCGetOutputTextMetrics(dc, metrics);
}

int CDCGetTextCharacterExtra(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetTextCharacterExtra(handle);
}

int CDCGetTextCharacterExtraInline(const MfcCDCCompat& dc) {
    return CDCGetTextCharacterExtra(dc);
}

BOOL CDCGetCharWidth(const MfcCDCCompat& dc, UINT first, UINT last,
    int* widths) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE : GetCharWidthA(handle, first, last, widths);
}

BOOL CDCGetCharWidthInline(const MfcCDCCompat& dc, UINT first, UINT last,
    int* widths) {
    return CDCGetCharWidth(dc, first, last, widths);
}

BOOL CDCGetOutputCharWidth(const MfcCDCCompat& dc, UINT first, UINT last,
    int* widths) {
    return dc.output_dc == nullptr ? FALSE
        : GetCharWidthA(dc.output_dc, first, last, widths);
}

BOOL CDCGetOutputCharWidthInline(const MfcCDCCompat& dc, UINT first,
    UINT last, int* widths) {
    return CDCGetOutputCharWidth(dc, first, last, widths);
}

SIZE CDCGetAspectRatioFilter(const MfcCDCCompat& dc) {
    SIZE size{};
    HDC handle = CDCAttributeOrOutput(dc);
    if (handle != nullptr) {
        GetAspectRatioFilterEx(handle, &size);
    }
    return size;
}

SIZE CDCGetAspectRatioFilterInline(const MfcCDCCompat& dc) {
    return CDCGetAspectRatioFilter(dc);
}

BOOL CDCScrollDC(MfcCDCCompat& dc, int dx, int dy, const RECT* scroll,
    const RECT* clip, HRGN update_region, RECT* update_rect) {
    return dc.output_dc == nullptr ? FALSE
        : ScrollDC(dc.output_dc, dx, dy, scroll, clip, update_region,
            update_rect);
}

BOOL CDCScrollDCInline(MfcCDCCompat& dc, int dx, int dy, const RECT* scroll,
    const RECT* clip, const MfcGdiObjectCompat* update_region,
    RECT* update_rect) {
    return CDCScrollDC(dc, dx, dy, scroll, clip,
        static_cast<HRGN>(GdiObjectGetSafeHandle(update_region)),
        update_rect);
}

int CDCEscape(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, void* output) {
    return dc.output_dc == nullptr ? 0
        : Escape(dc.output_dc, escape, input_size, input, output);
}

int CDCEscapeInline(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, void* output) {
    return CDCEscape(dc, escape, input_size, input, output);
}

UINT CDCSetBoundsRect(MfcCDCCompat& dc, const RECT* bounds, UINT flags) {
    return dc.output_dc == nullptr ? GDI_ERROR
        : SetBoundsRect(dc.output_dc, bounds, flags);
}

UINT CDCSetBoundsRectInline(MfcCDCCompat& dc, const RECT* bounds,
    UINT flags) {
    return CDCSetBoundsRect(dc, bounds, flags);
}

UINT CDCGetBoundsRect(const MfcCDCCompat& dc, RECT& bounds, UINT flags) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? GDI_ERROR : GetBoundsRect(handle, &bounds, flags);
}

UINT CDCGetBoundsRectInline(const MfcCDCCompat& dc, RECT& bounds,
    UINT flags) {
    return CDCGetBoundsRect(dc, bounds, flags);
}

bool CDCResetDC(MfcCDCCompat& dc, const DEVMODEA* devmode) {
    if (dc.attribute_dc == nullptr) {
        return false;
    }
    HDC reset = ResetDCA(dc.attribute_dc, devmode);
    if (reset == nullptr) {
        return false;
    }
    if (dc.output_dc == dc.attribute_dc) {
        dc.output_dc = reset;
    }
    dc.attribute_dc = reset;
    return true;
}

bool CDCResetDCInline(MfcCDCCompat& dc, const DEVMODEA* devmode) {
    return CDCResetDC(dc, devmode);
}

UINT CDCGetOutlineTextMetrics(const MfcCDCCompat& dc, UINT bytes,
    OUTLINETEXTMETRICA* metrics) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetOutlineTextMetricsA(handle, bytes,
        metrics);
}

UINT CDCGetOutlineTextMetricsInline(const MfcCDCCompat& dc, UINT bytes,
    OUTLINETEXTMETRICA* metrics) {
    return CDCGetOutlineTextMetrics(dc, bytes, metrics);
}

BOOL CDCGetCharABCWidths(const MfcCDCCompat& dc, UINT first, UINT last,
    ABC* widths) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE
        : GetCharABCWidthsA(handle, first, last, widths);
}

BOOL CDCGetCharABCWidthsInline(const MfcCDCCompat& dc, UINT first,
    UINT last, ABC* widths) {
    return CDCGetCharABCWidths(dc, first, last, widths);
}

DWORD CDCGetFontData(const MfcCDCCompat& dc, DWORD table, DWORD offset,
    void* buffer, DWORD bytes) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? GDI_ERROR
        : GetFontData(handle, table, offset, buffer, bytes);
}

DWORD CDCGetFontDataInline(const MfcCDCCompat& dc, DWORD table,
    DWORD offset, void* buffer, DWORD bytes) {
    return CDCGetFontData(dc, table, offset, buffer, bytes);
}

DWORD CDCGetKerningPairs(const MfcCDCCompat& dc, DWORD pairs,
    KERNINGPAIR* output) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetKerningPairsA(handle, pairs, output);
}

DWORD CDCGetKerningPairsInline(const MfcCDCCompat& dc, DWORD pairs,
    KERNINGPAIR* output) {
    return CDCGetKerningPairs(dc, pairs, output);
}

DWORD CDCGetGlyphOutline(const MfcCDCCompat& dc, UINT character, UINT format,
    GLYPHMETRICS* metrics, DWORD bytes, void* buffer, const MAT2* matrix) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? GDI_ERROR
        : GetGlyphOutlineA(handle, character, format, metrics, bytes, buffer,
            matrix);
}

DWORD CDCGetGlyphOutlineInline(const MfcCDCCompat& dc, UINT character,
    UINT format, GLYPHMETRICS* metrics, DWORD bytes, void* buffer,
    const MAT2* matrix) {
    return CDCGetGlyphOutline(dc, character, format, metrics, bytes, buffer,
        matrix);
}

int CDCStartDoc(MfcCDCCompat& dc, const DOCINFOA& info) {
    return dc.output_dc == nullptr ? 0
        : StartDocA(dc.output_dc, const_cast<DOCINFOA*>(&info));
}

int CDCStartDocInline(MfcCDCCompat& dc, const DOCINFOA& info) {
    return CDCStartDoc(dc, info);
}

int CDCStartPage(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? 0 : StartPage(dc.output_dc);
}

int CDCStartPageInline(MfcCDCCompat& dc) {
    return CDCStartPage(dc);
}

int CDCEndPage(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? 0 : EndPage(dc.output_dc);
}

int CDCEndPageInline(MfcCDCCompat& dc) {
    return CDCEndPage(dc);
}

int CDCSetAbortProcCompat(MfcCDCCompat& dc, ABORTPROC proc) {
    return dc.output_dc == nullptr ? 0 : SetAbortProc(dc.output_dc, proc);
}

int CDCSetAbortProcInline(MfcCDCCompat& dc, ABORTPROC proc) {
    return CDCSetAbortProcCompat(dc, proc);
}

int CDCAbortDoc(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? 0 : AbortDoc(dc.output_dc);
}

int CDCAbortDocInline(MfcCDCCompat& dc) {
    return CDCAbortDoc(dc);
}

int CDCEndDoc(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? 0 : EndDoc(dc.output_dc);
}

int CDCEndDocInline(MfcCDCCompat& dc) {
    return CDCEndDoc(dc);
}

BOOL CDCMaskBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, HBITMAP mask,
    int mask_x, int mask_y, DWORD rop) {
    return dc.output_dc == nullptr ? FALSE
        : MaskBlt(dc.output_dc, x, y, width, height, CDCGetSafeHdc(&source),
            src_x, src_y, mask, mask_x, mask_y, rop);
}

BOOL CDCMaskBltInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const MfcCDCCompat& source, int src_x, int src_y,
    HBITMAP mask, int mask_x, int mask_y, DWORD rop) {
    return CDCMaskBlt(dc, x, y, width, height, source, src_x, src_y, mask,
        mask_x, mask_y, rop);
}

BOOL CDCPlgBlt(MfcCDCCompat& dc, const POINT* points,
    const MfcCDCCompat& source, int src_x, int src_y, int width, int height,
    HBITMAP mask, int mask_x, int mask_y) {
    return dc.output_dc == nullptr ? FALSE
        : PlgBlt(dc.output_dc, points, CDCGetSafeHdc(&source), src_x, src_y,
            width, height, mask, mask_x, mask_y);
}

BOOL CDCPlgBltInline(MfcCDCCompat& dc, const POINT* points,
    const MfcCDCCompat& source, int src_x, int src_y, int width, int height,
    HBITMAP mask, int mask_x, int mask_y) {
    return CDCPlgBlt(dc, points, source, src_x, src_y, width, height, mask,
        mask_x, mask_y);
}

BOOL CDCSetPixelV(MfcCDCCompat& dc, int x, int y, COLORREF color) {
    return dc.output_dc == nullptr ? FALSE : SetPixelV(dc.output_dc, x, y, color);
}

BOOL CDCSetPixelVXYInline(MfcCDCCompat& dc, int x, int y, COLORREF color) {
    return CDCSetPixelV(dc, x, y, color);
}

BOOL CDCSetPixelVPoint(MfcCDCCompat& dc, POINT point, COLORREF color) {
    return CDCSetPixelV(dc, point.x, point.y, color);
}

BOOL CDCSetPixelVPointInline(MfcCDCCompat& dc, POINT point,
    COLORREF color) {
    return CDCSetPixelVPoint(dc, point, color);
}

BOOL CDCAngleArc(MfcCDCCompat& dc, int x, int y, DWORD radius,
    FLOAT start_angle, FLOAT sweep_angle) {
    return dc.output_dc == nullptr ? FALSE
        : AngleArc(dc.output_dc, x, y, radius, start_angle, sweep_angle);
}

BOOL CDCAngleArcInline(MfcCDCCompat& dc, int x, int y, DWORD radius,
    FLOAT start_angle, FLOAT sweep_angle) {
    return CDCAngleArc(dc, x, y, radius, start_angle, sweep_angle);
}

BOOL CDCArcToRect(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCArcTo(dc, rect.left, rect.top, rect.right, rect.bottom,
        x_start, y_start, x_end, y_end);
}

BOOL CDCArcToRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end) {
    return CDCArcToRect(dc, rect, x_start, y_start, x_end, y_end);
}

int CDCGetArcDirection(const MfcCDCCompat& dc) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? 0 : GetArcDirection(handle);
}

int CDCGetArcDirectionInline(const MfcCDCCompat& dc) {
    return CDCGetArcDirection(dc);
}

BOOL CDCPolyPolyline(MfcCDCCompat& dc, const POINT* points,
    const DWORD* poly_counts, DWORD count) {
    return dc.output_dc == nullptr ? FALSE
        : PolyPolyline(dc.output_dc, points, poly_counts, count);
}

BOOL CDCPolyPolylineInline(MfcCDCCompat& dc, const POINT* points,
    const DWORD* poly_counts, DWORD count) {
    return CDCPolyPolyline(dc, points, poly_counts, count);
}

BOOL CDCGetColorAdjustmentCompat(const MfcCDCCompat& dc,
    COLORADJUSTMENT& adjustment) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE : GetColorAdjustment(handle, &adjustment);
}

BOOL CDCGetColorAdjustmentInline(const MfcCDCCompat& dc,
    COLORADJUSTMENT& adjustment) {
    return CDCGetColorAdjustmentCompat(dc, adjustment);
}

HGDIOBJ CDCGetCurrentObjectCompat(const MfcCDCCompat& dc, UINT object_type) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? nullptr : GetCurrentObject(handle, object_type);
}

MfcGdiObjectCompat* CDCGetCurrentBrush(const MfcCDCCompat& dc) {
    return BrushFromHandle(static_cast<HBRUSH>(
        CDCGetCurrentObjectCompat(dc, OBJ_BRUSH)));
}

MfcGdiObjectCompat* CDCGetCurrentBrushInline(const MfcCDCCompat& dc) {
    return CDCGetCurrentBrush(dc);
}

MfcGdiObjectCompat* CDCGetCurrentPen(const MfcCDCCompat& dc) {
    return PenFromHandle(static_cast<HPEN>(
        CDCGetCurrentObjectCompat(dc, OBJ_PEN)));
}

MfcGdiObjectCompat* CDCGetCurrentPenInline(const MfcCDCCompat& dc) {
    return CDCGetCurrentPen(dc);
}

MfcGdiObjectCompat* CDCGetCurrentBitmap(const MfcCDCCompat& dc) {
    return BitmapFromHandle(static_cast<HBITMAP>(
        CDCGetCurrentObjectCompat(dc, OBJ_BITMAP)));
}

MfcGdiObjectCompat* CDCGetCurrentBitmapInline(const MfcCDCCompat& dc) {
    return CDCGetCurrentBitmap(dc);
}

MfcGdiObjectCompat* CDCGetCurrentPalette(const MfcCDCCompat& dc) {
    return PaletteFromHandle(static_cast<HPALETTE>(
        CDCGetCurrentObjectCompat(dc, OBJ_PAL)));
}

MfcGdiObjectCompat* CDCGetCurrentPaletteInline(const MfcCDCCompat& dc) {
    return CDCGetCurrentPalette(dc);
}

MfcGdiObjectCompat* CDCGetCurrentFont(const MfcCDCCompat& dc) {
    return FontFromHandle(static_cast<HFONT>(
        CDCGetCurrentObjectCompat(dc, OBJ_FONT)));
}

MfcGdiObjectCompat* CDCGetCurrentFontInline(const MfcCDCCompat& dc) {
    return CDCGetCurrentFont(dc);
}

BOOL CDCPolyBezier(MfcCDCCompat& dc, const POINT* points, DWORD count) {
    return dc.output_dc == nullptr ? FALSE : PolyBezier(dc.output_dc, points,
        count);
}

BOOL CDCPolyBezierInline(MfcCDCCompat& dc, const POINT* points, DWORD count) {
    return CDCPolyBezier(dc, points, count);
}

int CDCDrawEscape(MfcCDCCompat& dc, int escape, int input_size,
    const char* input) {
    return dc.output_dc == nullptr ? 0
        : DrawEscape(dc.output_dc, escape, input_size, input);
}

int CDCDrawEscapeInline(MfcCDCCompat& dc, int escape, int input_size,
    const char* input) {
    return CDCDrawEscape(dc, escape, input_size, input);
}

int CDCExtEscape(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, int output_size, char* output) {
    return dc.output_dc == nullptr ? 0
        : ExtEscape(dc.output_dc, escape, input_size, input, output_size,
            output);
}

int CDCExtEscapeInline(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, int output_size, char* output) {
    return CDCExtEscape(dc, escape, input_size, input, output_size, output);
}

BOOL CDCGetCharABCWidthsFloat(const MfcCDCCompat& dc, UINT first, UINT last,
    ABCFLOAT* widths) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE
        : GetCharABCWidthsFloatA(handle, first, last, widths);
}

BOOL CDCGetCharABCWidthsFloatInline(const MfcCDCCompat& dc, UINT first,
    UINT last, ABCFLOAT* widths) {
    return CDCGetCharABCWidthsFloat(dc, first, last, widths);
}

BOOL CDCGetCharWidthFloat(const MfcCDCCompat& dc, UINT first, UINT last,
    FLOAT* widths) {
    HDC handle = CDCAttributeOrOutput(dc);
    return handle == nullptr ? FALSE
        : GetCharWidthFloatA(handle, first, last, widths);
}

BOOL CDCGetCharWidthFloatInline(const MfcCDCCompat& dc, UINT first,
    UINT last, FLOAT* widths) {
    return CDCGetCharWidthFloat(dc, first, last, widths);
}

BOOL CDCAbortPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : AbortPath(dc.output_dc);
}

BOOL CDCAbortPathInline(MfcCDCCompat& dc) {
    return CDCAbortPath(dc);
}

BOOL CDCBeginPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : BeginPath(dc.output_dc);
}

BOOL CDCBeginPathInline(MfcCDCCompat& dc) {
    return CDCBeginPath(dc);
}

BOOL CDCCloseFigure(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : CloseFigure(dc.output_dc);
}

BOOL CDCCloseFigureInline(MfcCDCCompat& dc) {
    return CDCCloseFigure(dc);
}

BOOL CDCEndPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : EndPath(dc.output_dc);
}

BOOL CDCEndPathInline(MfcCDCCompat& dc) {
    return CDCEndPath(dc);
}

BOOL CDCFillPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : FillPath(dc.output_dc);
}

BOOL CDCFillPathInline(MfcCDCCompat& dc) {
    return CDCFillPath(dc);
}

BOOL CDCFlattenPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : FlattenPath(dc.output_dc);
}

BOOL CDCFlattenPathInline(MfcCDCCompat& dc) {
    return CDCFlattenPath(dc);
}

FLOAT CDCGetMiterLimit(MfcCDCCompat& dc) {
    FLOAT limit = 0.0f;
    if (dc.output_dc != nullptr) {
        GetMiterLimit(dc.output_dc, &limit);
    }
    return limit;
}

FLOAT CDCGetMiterLimitInline(MfcCDCCompat& dc) {
    return CDCGetMiterLimit(dc);
}

int CDCGetPath(MfcCDCCompat& dc, POINT* points, BYTE* types, int count) {
    return dc.output_dc == nullptr ? 0 : GetPath(dc.output_dc, points, types,
        count);
}

int CDCGetPathInline(MfcCDCCompat& dc, POINT* points, BYTE* types,
    int count) {
    return CDCGetPath(dc, points, types, count);
}

BOOL CDCSetMiterLimit(MfcCDCCompat& dc, FLOAT limit) {
    return dc.output_dc == nullptr ? FALSE : SetMiterLimit(dc.output_dc, limit,
        nullptr);
}

BOOL CDCSetMiterLimitInline(MfcCDCCompat& dc, FLOAT limit) {
    return CDCSetMiterLimit(dc, limit);
}

BOOL CDCStrokeAndFillPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : StrokeAndFillPath(dc.output_dc);
}

BOOL CDCStrokeAndFillPathInline(MfcCDCCompat& dc) {
    return CDCStrokeAndFillPath(dc);
}

BOOL CDCStrokePath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : StrokePath(dc.output_dc);
}

BOOL CDCStrokePathInline(MfcCDCCompat& dc) {
    return CDCStrokePath(dc);
}

BOOL CDCWidenPath(MfcCDCCompat& dc) {
    return dc.output_dc == nullptr ? FALSE : WidenPath(dc.output_dc);
}

BOOL CDCWidenPathInline(MfcCDCCompat& dc) {
    return CDCWidenPath(dc);
}

BOOL CDCGdiComment(MfcCDCCompat& dc, UINT bytes, const BYTE* data) {
    return dc.output_dc == nullptr ? FALSE : GdiComment(dc.output_dc, bytes,
        data);
}

BOOL CDCGdiCommentInline(MfcCDCCompat& dc, UINT bytes, const BYTE* data) {
    return CDCGdiComment(dc, bytes, data);
}

BOOL CDCPlayEnhMetaFile(MfcCDCCompat& dc, HENHMETAFILE metafile,
    const RECT& bounds) {
    return dc.output_dc == nullptr ? FALSE
        : PlayEnhMetaFile(dc.output_dc, metafile, &bounds);
}

BOOL CDCPlayEnhMetaFileInline(MfcCDCCompat& dc, HENHMETAFILE metafile,
    const RECT& bounds) {
    return CDCPlayEnhMetaFile(dc, metafile, bounds);
}

static HDC CDCTextMetricHandle(const MfcCDCCompat& dc) {
    return dc.attribute_dc != nullptr ? dc.attribute_dc : dc.output_dc;
}

SIZE CDCGetTextExtentString(MfcCDCCompat& dc, const MfcCStringCompat& text) {
    SIZE size{};
    HDC handle = CDCTextMetricHandle(dc);
    if (handle != nullptr) {
        GetTextExtentPoint32A(handle, text.text.c_str(),
            static_cast<int>(text.text.size()), &size);
    }
    return size;
}

SIZE CDCGetTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text) {
    return CDCGetTextExtentString(dc, text);
}

SIZE CDCGetOutputTextExtentString(MfcCDCCompat& dc,
    const MfcCStringCompat& text) {
    SIZE size{};
    if (dc.output_dc != nullptr) {
        GetTextExtentPoint32A(dc.output_dc, text.text.c_str(),
            static_cast<int>(text.text.size()), &size);
    }
    return size;
}

SIZE CDCGetOutputTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text) {
    return CDCGetOutputTextExtentString(dc, text);
}

BOOL CDCTextOutString(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    return TextOutA(dc.output_dc, x, y, text.text.c_str(),
        static_cast<int>(text.text.size()));
}

BOOL CDCTextOutCStringInline(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text) {
    return CDCTextOutString(dc, x, y, text);
}

BOOL CDCExtTextOutString(MfcCDCCompat& dc, int x, int y, UINT options,
    const RECT* rect, const MfcCStringCompat& text, const INT* dx) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    return ExtTextOutA(dc.output_dc, x, y, options, rect, text.text.c_str(),
        static_cast<UINT>(text.text.size()), dx);
}

BOOL CDCExtTextOutCStringInline(MfcCDCCompat& dc, int x, int y,
    UINT options, const RECT* rect, const MfcCStringCompat& text,
    const INT* dx) {
    return CDCExtTextOutString(dc, x, y, options, rect, text, dx);
}

SIZE CDCTabbedTextOutString(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions,
    int tab_origin) {
    LONG packed = 0;
    if (dc.output_dc != nullptr) {
        packed = TabbedTextOutA(dc.output_dc, x, y, text.text.c_str(),
            static_cast<int>(text.text.size()), tab_count, tab_positions,
            tab_origin);
    }
    return SizeConstructFromDWord(static_cast<DWORD>(packed));
}

SIZE CDCTabbedTextOutCStringInline(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions,
    int tab_origin) {
    return CDCTabbedTextOutString(dc, x, y, text, tab_count, tab_positions,
        tab_origin);
}

int CDCDrawTextString(MfcCDCCompat& dc, const MfcCStringCompat& text,
    RECT& rect, UINT format) {
    if (dc.output_dc == nullptr) {
        return 0;
    }
    return DrawTextA(dc.output_dc, text.text.c_str(),
        static_cast<int>(text.text.size()), &rect, format);
}

int CDCDrawTextCStringInline(MfcCDCCompat& dc, const MfcCStringCompat& text,
    RECT& rect, UINT format) {
    return CDCDrawTextString(dc, text, rect, format);
}

UINT CDCSetTextAlign(MfcCDCCompat& dc, UINT flags) {
    UINT previous = GDI_ERROR;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetTextAlign(dc.output_dc, flags);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetTextAlign(dc.attribute_dc, flags);
    }
    return previous;
}

BOOL CDCSetTextJustification(MfcCDCCompat& dc, int break_extra,
    int break_count) {
    BOOL result = FALSE;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = SetTextJustification(dc.output_dc, break_extra, break_count);
    }
    if (dc.attribute_dc != nullptr) {
        result = SetTextJustification(dc.attribute_dc, break_extra, break_count);
    }
    return result;
}

int CDCSetTextCharacterExtra(MfcCDCCompat& dc, int extra) {
    int previous = 0x08000000;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetTextCharacterExtra(dc.output_dc, extra);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetTextCharacterExtra(dc.attribute_dc, extra);
    }
    return previous;
}

DWORD CDCSetMapperFlags(MfcCDCCompat& dc, DWORD flags) {
    DWORD previous = GDI_ERROR;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetMapperFlags(dc.output_dc, flags);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetMapperFlags(dc.attribute_dc, flags);
    }
    return previous;
}

DWORD CDCGetLayoutCompat(MfcCDCCompat& dc) {
    if (dc.output_dc == nullptr) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GDI_ERROR;
    }
    return GetLayout(dc.output_dc);
}

DWORD CDCSetLayoutCompat(MfcCDCCompat& dc, DWORD layout) {
    if (dc.output_dc == nullptr) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GDI_ERROR;
    }
    return SetLayout(dc.output_dc, layout);
}

void WindowScreenToClientRect(HWND window, RECT& rect) {
    if (window == nullptr) {
        return;
    }
    ScreenToClient(window, reinterpret_cast<POINT*>(&rect.left));
    ScreenToClient(window, reinterpret_cast<POINT*>(&rect.right));
    if ((GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0) {
        std::swap(rect.left, rect.right);
    }
}

void WindowClientToScreenRect(HWND window, RECT& rect) {
    if (window == nullptr) {
        return;
    }
    ClientToScreen(window, reinterpret_cast<POINT*>(&rect.left));
    ClientToScreen(window, reinterpret_cast<POINT*>(&rect.right));
    if ((GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0) {
        std::swap(rect.left, rect.right);
    }
}

BOOL CDCArcTo(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    BOOL result = ArcTo(dc.output_dc, left, top, right, bottom, x_start,
        y_start, x_end, y_end);
    CDCSyncAttributePosition(dc);
    return result;
}

int CDCSetArcDirection(MfcCDCCompat& dc, int direction) {
    int previous = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        previous = SetArcDirection(dc.output_dc, direction);
    }
    if (dc.attribute_dc != nullptr) {
        previous = SetArcDirection(dc.attribute_dc, direction);
    }
    return previous;
}

BOOL CDCPolyDraw(MfcCDCCompat& dc, const POINT* points, const BYTE* types,
    int count) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    BOOL result = PolyDraw(dc.output_dc, points, types, count);
    CDCSyncAttributePosition(dc);
    return result;
}

BOOL CDCPolylineTo(MfcCDCCompat& dc, const POINT* points, DWORD count) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    BOOL result = PolylineTo(dc.output_dc, points, count);
    CDCSyncAttributePosition(dc);
    return result;
}

BOOL CDCSetColorAdjustmentCompat(MfcCDCCompat& dc,
    const COLORADJUSTMENT& adjustment) {
    BOOL result = FALSE;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = SetColorAdjustment(dc.output_dc, &adjustment);
    }
    if (dc.attribute_dc != nullptr) {
        result = SetColorAdjustment(dc.attribute_dc, &adjustment);
    }
    return result;
}

BOOL CDCPolyBezierTo(MfcCDCCompat& dc, const POINT* points, DWORD count) {
    if (dc.output_dc == nullptr) {
        return FALSE;
    }
    BOOL result = PolyBezierTo(dc.output_dc, points, count);
    CDCSyncAttributePosition(dc);
    return result;
}

bool CDCSelectClipPathCompat(MfcCDCCompat& dc, int mode) {
    if (dc.output_dc == nullptr || !SelectClipPath(dc.output_dc, mode)) {
        return false;
    }
    if (dc.attribute_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        if (region == nullptr) {
            return false;
        }
        const int got_region = GetClipRgn(dc.output_dc, region);
        const bool ok = got_region >= 0 &&
            SelectClipRgn(dc.attribute_dc, region) != 0;
        DeleteObject(region);
        return ok;
    }
    return true;
}

int CDCExtSelectClipRgn(MfcCDCCompat& dc, HRGN region, int mode) {
    int result = 0;
    if (dc.output_dc != nullptr && dc.output_dc != dc.attribute_dc) {
        result = ExtSelectClipRgn(dc.output_dc, region, mode);
    }
    if (dc.attribute_dc != nullptr) {
        result = ExtSelectClipRgn(dc.attribute_dc, region, mode);
    }
    return result;
}

namespace {

int MetaRecordInt16(const METARECORD* record, int index) {
    return static_cast<SHORT>(record->rdParm[index]);
}

DWORD MetaRecordDword(const METARECORD* record, int index) {
    return static_cast<DWORD>(record->rdParm[index]) |
        (static_cast<DWORD>(record->rdParm[index + 1]) << 16);
}

int PositiveExtent(int value) {
    if (value == 0) {
        return 1;
    }
    return value < 0 ? -value : value;
}

HDC CDCMetricHandle(const MfcCDCCompat& dc) {
    return dc.attribute_dc != nullptr ? dc.attribute_dc : dc.output_dc;
}

bool CDCGetMappingExtents(const MfcCDCCompat& dc, SIZE& window_ext,
    SIZE& viewport_ext) {
    HDC handle = CDCMetricHandle(dc);
    if (handle == nullptr) {
        return false;
    }
    return GetWindowExtEx(handle, &window_ext) != FALSE &&
        GetViewportExtEx(handle, &viewport_ext) != FALSE;
}

void CDCResetWindowStorage(MfcWindowDCCompat& dc) {
    dc.runtime_class = GetCObjectRuntimeClass();
    dc.output_dc = nullptr;
    dc.attribute_dc = nullptr;
    dc.printing = false;
    dc.window = nullptr;
    std::memset(&dc.paint, 0, sizeof(dc.paint));
}

HDC CDCDetachHandle(MfcCDCCompat& dc) {
    HDC handle = dc.output_dc;
    if (dc.output_dc != nullptr) {
        g_dc_handle_map.erase(dc.output_dc);
    }
    if (dc.attribute_dc != nullptr && dc.attribute_dc != dc.output_dc) {
        g_dc_handle_map.erase(dc.attribute_dc);
    }
    dc.output_dc = nullptr;
    dc.attribute_dc = nullptr;
    dc.printing = false;
    return handle;
}

void CDCTraceInvalidWindow(const char* type, HWND window) {
    AfxTraceOutput("%s has an invalid HWND %p\n", type, window);
}

} // namespace

HMETAFILE MetaFileDCClose(MfcCDCCompat& dc) {
    HDC handle = CDCDetachHandle(dc);
    return handle != nullptr ? CloseMetaFile(handle) : nullptr;
}

int CALLBACK CDCMetaFileEnumProc(HDC dc, HANDLETABLE* table,
    METARECORD* record, int object_count, LPARAM data) {
    auto* target = reinterpret_cast<MfcCDCCompat*>(data);
    if (target == nullptr || record == nullptr) {
        if (record != nullptr) {
            PlayMetaFileRecord(dc, table, record, static_cast<UINT>(object_count));
        }
        return 1;
    }

    switch (record->rdFunction) {
    case 0x001e: // META_SAVEDC
        CDCSaveDC(*target);
        return 1;
    case 0x0103: // META_SETMAPMODE
        CDCSetMapMode(*target, MetaRecordInt16(record, 0));
        return 1;
    case 0x0127: // META_RESTOREDC
        CDCRestoreDC(*target, MetaRecordInt16(record, 0));
        return 1;
    case 0x012d: { // META_SELECTOBJECT
        const unsigned index = static_cast<unsigned>(record->rdParm[0]);
        if (table != nullptr && index < static_cast<unsigned>(object_count)) {
            HGDIOBJ object = table->objectHandle[index];
            if (object != nullptr && GetObjectType(object) == OBJ_FONT) {
                CDCSelectFont(*target, object);
                return 1;
            }
        }
        break;
    }
    case 0x0201: // META_SETBKCOLOR
        CDCSetBkColor(*target, MetaRecordDword(record, 0));
        return 1;
    case 0x0209: // META_SETTEXTCOLOR
        CDCSetTextColor(*target, MetaRecordDword(record, 0));
        return 1;
    case 0x020b: // META_SETWINDOWORG
        CDCSetWindowOrg(*target, MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    case 0x020c: // META_SETWINDOWEXT
        CDCSetWindowExt(*target, MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    case 0x020d: // META_SETVIEWPORTORG
        CDCSetViewportOrg(*target, MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    case 0x020e: // META_SETVIEWPORTEXT
        CDCSetViewportExt(*target, MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    case 0x0211: // META_OFFSETVIEWPORTORG
        CDCOffsetViewportOrg(*target, MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    case 0x0410: // META_SCALEWINDOWEXT
        CDCScaleWindowExt(*target, MetaRecordInt16(record, 3),
            MetaRecordInt16(record, 2), MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    case 0x0412: // META_SCALEVIEWPORTEXT
        CDCScaleViewportExt(*target, MetaRecordInt16(record, 3),
            MetaRecordInt16(record, 2), MetaRecordInt16(record, 1),
            MetaRecordInt16(record, 0));
        return 1;
    default:
        break;
    }

    PlayMetaFileRecord(dc, table, record, static_cast<UINT>(object_count));
    return 1;
}

void CDCHIMETRICToDevice(MfcCDCCompat& dc, SIZE& size) {
    SIZE window_ext{};
    SIZE viewport_ext{};
    if (!CDCGetMappingExtents(dc, window_ext, viewport_ext)) {
        return;
    }
    size.cx = MulDiv(size.cx, PositiveExtent(viewport_ext.cx),
        PositiveExtent(window_ext.cx));
    size.cy = MulDiv(size.cy, PositiveExtent(viewport_ext.cy),
        PositiveExtent(window_ext.cy));
}

void CDCDeviceToHIMETRIC(MfcCDCCompat& dc, SIZE& size) {
    SIZE window_ext{};
    SIZE viewport_ext{};
    if (!CDCGetMappingExtents(dc, window_ext, viewport_ext)) {
        return;
    }
    size.cx = MulDiv(size.cx, PositiveExtent(window_ext.cx),
        PositiveExtent(viewport_ext.cx));
    size.cy = MulDiv(size.cy, PositiveExtent(window_ext.cy),
        PositiveExtent(viewport_ext.cy));
}

void DeleteHalftoneBrush() {
    if (g_halftone_brush != nullptr) {
        DeleteObject(g_halftone_brush);
        g_halftone_brush = nullptr;
    }
}

void RegisterHalftoneBrushCleanupThunk() {
    RegisterHalftoneBrushCleanup();
}

void RegisterHalftoneBrushCleanup() {
    CrtAtexit(DeleteHalftoneBrush);
}

HBRUSH GetHalftoneBrush() {
    if (g_halftone_brush == nullptr) {
        WORD bits[8]{};
        for (int index = 0; index < 8; ++index) {
            bits[index] = static_cast<WORD>(0x5555U << (index & 1));
        }
        HBITMAP bitmap = CreateBitmap(8, 8, 1, 1, bits);
        if (bitmap != nullptr) {
            g_halftone_brush = CreatePatternBrush(bitmap);
            DeleteObject(bitmap);
        }
    }
    return g_halftone_brush;
}

namespace {

HDC metric_dc_or_screen(MfcCDCCompat* dc, bool& release_screen) {
    release_screen = false;
    if (dc != nullptr) {
        HDC handle = CDCMetricHandle(*dc);
        if (handle != nullptr) {
            return handle;
        }
    }
    release_screen = true;
    return GetDC(nullptr);
}

void release_metric_dc(HDC handle, bool release_screen) {
    if (release_screen && handle != nullptr) {
        ReleaseDC(nullptr, handle);
    }
}

} // namespace

void CDCDPtoHIMETRIC(MfcCDCCompat* dc, SIZE& size) {
    bool release_screen = false;
    HDC handle = metric_dc_or_screen(dc, release_screen);
    if (handle == nullptr) {
        return;
    }
    int map_mode = GetMapMode(handle);
    if (dc == nullptr || map_mode == MM_TEXT || map_mode > MM_TWIPS) {
        const int dpi_x = GetDeviceCaps(handle, LOGPIXELSX);
        const int dpi_y = GetDeviceCaps(handle, LOGPIXELSY);
        if (dpi_x != 0 && dpi_y != 0) {
            size.cx = MulDiv(size.cx, 2540, dpi_x);
            size.cy = MulDiv(size.cy, 2540, dpi_y);
        }
    } else {
        const int old_mode = SetMapMode(handle, MM_HIMETRIC);
        POINT points[2]{{0, 0}, {size.cx, size.cy}};
        DPtoLP(handle, points, 2);
        size.cx = points[1].x - points[0].x;
        size.cy = points[1].y - points[0].y;
        SetMapMode(handle, old_mode);
    }
    release_metric_dc(handle, release_screen);
}

void CDCHIMETRICtoDP(MfcCDCCompat* dc, SIZE& size) {
    bool release_screen = false;
    HDC handle = metric_dc_or_screen(dc, release_screen);
    if (handle == nullptr) {
        return;
    }
    int map_mode = GetMapMode(handle);
    if (dc == nullptr || map_mode == MM_TEXT || map_mode > MM_TWIPS) {
        const int dpi_x = GetDeviceCaps(handle, LOGPIXELSX);
        const int dpi_y = GetDeviceCaps(handle, LOGPIXELSY);
        size.cx = MulDiv(size.cx, dpi_x, 2540);
        size.cy = MulDiv(size.cy, dpi_y, 2540);
    } else {
        const int old_mode = SetMapMode(handle, MM_HIMETRIC);
        POINT points[2]{{0, 0}, {size.cx, size.cy}};
        LPtoDP(handle, points, 2);
        size.cx = points[1].x - points[0].x;
        size.cy = points[1].y - points[0].y;
        SetMapMode(handle, old_mode);
    }
    release_metric_dc(handle, release_screen);
}

void CDCLPtoHIMETRIC(MfcCDCCompat& dc, SIZE& size) {
    HDC handle = CDCMetricHandle(dc);
    if (handle != nullptr) {
        POINT points[2]{{0, 0}, {size.cx, size.cy}};
        LPtoDP(handle, points, 2);
        size.cx = points[1].x - points[0].x;
        size.cy = points[1].y - points[0].y;
    }
    CDCDPtoHIMETRIC(&dc, size);
}

void CDCHIMETRICtoLP(MfcCDCCompat& dc, SIZE& size) {
    CDCHIMETRICtoDP(&dc, size);
    HDC handle = CDCMetricHandle(dc);
    if (handle != nullptr) {
        POINT points[2]{{0, 0}, {size.cx, size.cy}};
        DPtoLP(handle, points, 2);
        size.cx = points[1].x - points[0].x;
        size.cy = points[1].y - points[0].y;
    }
}

void CDCFillSolidRect(MfcCDCCompat& dc, const RECT& rect, COLORREF color) {
    if (dc.output_dc == nullptr) {
        return;
    }
    SetBkColor(dc.output_dc, color);
    ExtTextOutA(dc.output_dc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
}

void CDCFillSolidRectXY(MfcCDCCompat& dc, int x, int y, int width,
    int height, COLORREF color) {
    RECT rect{x, y, x + width, y + height};
    CDCFillSolidRect(dc, rect, color);
}

void Draw3dRect(MfcCDCCompat& dc, int x, int y, int width, int height,
    COLORREF top_left, COLORREF bottom_right) {
    CDCFillSolidRectXY(dc, x, y, width - 1, 1, top_left);
    CDCFillSolidRectXY(dc, x, y, 1, height - 1, top_left);
    CDCFillSolidRectXY(dc, x + width, y, -1, height, bottom_right);
    CDCFillSolidRectXY(dc, x, y + height, width, -1, bottom_right);
}

void CDCDrawDragRect(MfcCDCCompat& dc, const RECT& rect, SIZE size,
    const RECT* last_rect, SIZE last_size, HBRUSH brush,
    HBRUSH last_brush) {
    if (dc.output_dc == nullptr) {
        return;
    }
    HBRUSH new_brush = brush != nullptr ? brush : GetHalftoneBrush();
    HBRUSH old_brush = last_brush != nullptr ? last_brush : new_brush;
    auto draw_frame = [&](const RECT& source, SIZE frame_size, HBRUSH use_brush) {
        RECT draw = source;
        if (frame_size.cx <= 0) {
            frame_size.cx = 1;
        }
        if (frame_size.cy <= 0) {
            frame_size.cy = 1;
        }
        FrameRect(dc.output_dc, &draw, use_brush);
        InflateRect(&draw, -frame_size.cx, -frame_size.cy);
        if (draw.left < draw.right && draw.top < draw.bottom) {
            FrameRect(dc.output_dc, &draw, use_brush);
        }
    };
    if (last_rect != nullptr) {
        draw_frame(*last_rect, last_size, old_brush);
    }
    draw_frame(rect, size, new_brush);
}

bool FontCreatePointFontIndirect(MfcGdiObjectCompat& font, LOGFONTA log_font,
    MfcCDCCompat* dc) {
    bool release_screen = false;
    HDC handle = metric_dc_or_screen(dc, release_screen);
    if (handle == nullptr) {
        return false;
    }
    const int dpi_y = GetDeviceCaps(handle, LOGPIXELSY);
    POINT point{0, MulDiv(log_font.lfHeight, dpi_y, 720)};
    DPtoLP(handle, &point, 1);
    POINT origin{0, 0};
    DPtoLP(handle, &origin, 1);
    log_font.lfHeight = -std::abs(point.y - origin.y);
    release_metric_dc(handle, release_screen);

    GdiObjectDeleteObject(font);
    HFONT handle_font = CreateFontIndirectA(&log_font);
    return handle_font != nullptr && GdiObjectAttach(font, handle_font);
}

bool FontCreatePointFont(MfcGdiObjectCompat& font, int point_size,
    const char* face_name, MfcCDCCompat* dc) {
    if (face_name == nullptr) {
        return false;
    }
    LOGFONTA log_font{};
    log_font.lfHeight = point_size;
    log_font.lfCharSet = DEFAULT_CHARSET;
    lstrcpynA(log_font.lfFaceName, face_name, LF_FACESIZE);
    return FontCreatePointFontIndirect(font, log_font, dc);
}

namespace {

HBRUSH g_rect_tracker_brush = nullptr;
HPEN g_rect_tracker_pen = nullptr;
std::array<HCURSOR, 10> g_rect_tracker_cursors{};
bool g_rect_tracker_statics_initialized = false;

int rect_tracker_width(const RECT& rect) {
    return rect.right - rect.left;
}

int rect_tracker_height(const RECT& rect) {
    return rect.bottom - rect.top;
}

RECT normalized_rect(RECT rect) {
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

} // namespace

void DeleteRectTrackerStatics() {
    if (g_rect_tracker_brush != nullptr) {
        DeleteObject(g_rect_tracker_brush);
        g_rect_tracker_brush = nullptr;
    }
    if (g_rect_tracker_pen != nullptr) {
        DeleteObject(g_rect_tracker_pen);
        g_rect_tracker_pen = nullptr;
    }
    g_rect_tracker_cursors = {};
    g_rect_tracker_statics_initialized = false;
}

void RegisterRectTrackerStaticsThunk() {
    RegisterRectTrackerStatics();
}

void RegisterRectTrackerStatics() {
    CrtAtexit(DeleteRectTrackerStatics);
}

MfcRectTrackerCompat& ConstructRectTrackerDefault(
    MfcRectTrackerCompat& tracker) {
    RectTrackerConstructCommon(tracker);
    return tracker;
}

MfcRectTrackerCompat& ConstructRectTracker(MfcRectTrackerCompat& tracker,
    const RECT& rect, UINT style) {
    RectTrackerConstructCommon(tracker);
    tracker.rect = rect;
    tracker.style = style;
    return tracker;
}

void RectTrackerConstructCommon(MfcRectTrackerCompat& tracker) {
    if (!g_rect_tracker_statics_initialized) {
        if (g_rect_tracker_brush == nullptr) {
            WORD pattern[8]{0x1111, 0x2222, 0x4444, 0x8888,
                0x1111, 0x2222, 0x4444, 0x8888};
            HBITMAP bitmap = CreateBitmap(8, 8, 1, 1, pattern);
            if (bitmap != nullptr) {
                g_rect_tracker_brush = CreatePatternBrush(bitmap);
                DeleteObject(bitmap);
            }
        }
        if (g_rect_tracker_pen == nullptr) {
            g_rect_tracker_pen = CreatePen(PS_INSIDEFRAME, 0, RGB(0, 0, 0));
        }
        g_rect_tracker_cursors[0] = LoadCursorA(nullptr, IDC_SIZENWSE);
        g_rect_tracker_cursors[1] = LoadCursorA(nullptr, IDC_SIZENS);
        g_rect_tracker_cursors[2] = LoadCursorA(nullptr, IDC_SIZENESW);
        g_rect_tracker_cursors[3] = LoadCursorA(nullptr, IDC_SIZEWE);
        g_rect_tracker_cursors[4] = LoadCursorA(nullptr, IDC_SIZENWSE);
        g_rect_tracker_cursors[5] = LoadCursorA(nullptr, IDC_SIZENS);
        g_rect_tracker_cursors[6] = LoadCursorA(nullptr, IDC_SIZENESW);
        g_rect_tracker_cursors[7] = LoadCursorA(nullptr, IDC_SIZEWE);
        g_rect_tracker_cursors[8] = LoadCursorA(nullptr, IDC_SIZEALL);
        g_rect_tracker_cursors[9] = LoadCursorA(nullptr, IDC_ARROW);
        g_rect_tracker_statics_initialized = true;
        RegisterRectTrackerStatics();
    }
    tracker.style = 0;
    tracker.min_size = SIZE{};
    tracker.handle_size = GetProfileIntA("windows",
        "oleinplaceborderwidth", 4);
    tracker.resize_outside_size = tracker.handle_size * 2;
    tracker.border_size = tracker.resize_outside_size;
    tracker.last_rect = RECT{};
    tracker.last_size = SIZE{};
    tracker.tracking = false;
    tracker.final_erase = false;
    tracker.clip_window = nullptr;
}

void DestroyRectTracker(MfcRectTrackerCompat& tracker) {
    (void)tracker;
}

MfcRectTrackerCompat* DeleteRectTrackerScalarDtor(
    MfcRectTrackerCompat* tracker, unsigned flags) {
    if (tracker == nullptr) {
        return nullptr;
    }
    DestroyRectTracker(*tracker);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteNormalBlock(tracker);
        return nullptr;
    }
    return tracker;
}

void RectTrackerDraw(MfcRectTrackerCompat& tracker, MfcCDCCompat& dc) {
    RECT rect = normalized_rect(tracker.rect);
    if ((tracker.style & 3U) != 0 && dc.output_dc != nullptr) {
        HGDIOBJ old_pen = SelectObject(dc.output_dc,
            (tracker.style & 2U) != 0 && g_rect_tracker_pen != nullptr
                ? static_cast<HGDIOBJ>(g_rect_tracker_pen)
                : GetStockObject(BLACK_PEN));
        HGDIOBJ old_brush = SelectObject(dc.output_dc,
            GetStockObject(NULL_BRUSH));
        const int old_rop = SetROP2(dc.output_dc, R2_NOTXORPEN);
        Rectangle(dc.output_dc, rect.left, rect.top, rect.right, rect.bottom);
        SetROP2(dc.output_dc, old_rop);
        if (old_pen != nullptr) {
            SelectObject(dc.output_dc, old_pen);
        }
        if (old_brush != nullptr) {
            SelectObject(dc.output_dc, old_brush);
        }
    }
    if ((tracker.style & 0x18U) != 0) {
        for (int handle = 0; handle < 8; ++handle) {
            RECT handle_rect{};
            RectTrackerGetHandleRect(tracker, handle, handle_rect);
            CDCFillSolidRect(dc, handle_rect, RGB(0, 0, 0));
        }
    }
}

bool RectTrackerSetCursor(MfcRectTrackerCompat& tracker,
    MfcCWndCompat& window, UINT hit_test) {
    if (hit_test != HTCLIENT) {
        return false;
    }
    POINT point{};
    GetCursorPos(&point);
    if (window.window != nullptr) {
        ScreenToClient(window.window, &point);
    }
    int hit = RectTrackerHitTestHandles(tracker, point);
    if (hit < 0) {
        return false;
    }
    hit = RectTrackerNormalizeHit(tracker, hit);
    if (hit == 8 && (tracker.style & 4U) != 0) {
        RECT rect{};
        RectTrackerGetTrueRect(tracker, rect);
        if (!PtInRect(&rect, point)) {
            hit = 9;
        }
    }
    HCURSOR cursor = (0 <= hit && hit < static_cast<int>(
        g_rect_tracker_cursors.size())) ? g_rect_tracker_cursors[hit] : nullptr;
    SetCursor(cursor != nullptr ? cursor : LoadCursorA(nullptr, IDC_ARROW));
    return true;
}

int RectTrackerHitTest(MfcRectTrackerCompat& tracker, POINT point) {
    RECT rect{};
    RectTrackerGetTrueRect(tracker, rect);
    if (!PtInRect(&rect, point)) {
        return -1;
    }
    if ((tracker.style & 0x18U) != 0) {
        return RectTrackerHitTestHandles(tracker, point);
    }
    return 8;
}

int RectTrackerNormalizeHit(MfcRectTrackerCompat& tracker, int hit) {
    if (hit != -1 && hit != 8) {
        if (rect_tracker_width(tracker.rect) < 0) {
            static constexpr int mirror_x[8]{2, 1, 0, 7, 6, 5, 4, 3};
            hit = mirror_x[hit & 7];
        }
        if (rect_tracker_height(tracker.rect) < 0) {
            static constexpr int mirror_y[8]{6, 5, 4, 3, 2, 1, 0, 7};
            hit = mirror_y[hit & 7];
        }
    }
    return hit;
}

void RectTrackerDrawTrackerRect(MfcRectTrackerCompat& tracker,
    const RECT& rect, MfcCWndCompat* clip_window, MfcCDCCompat& dc,
    MfcCWndCompat* window) {
    RECT draw = normalized_rect(rect);
    if (clip_window != nullptr && clip_window->window != nullptr &&
        window != nullptr && window->window != nullptr) {
        WindowClientToScreenRect(window->window, draw);
        WindowScreenToClientRect(clip_window->window, draw);
    }
    SIZE size{std::max(1, tracker.border_size),
        std::max(1, tracker.border_size)};
    CDCDrawDragRect(dc, draw, size, nullptr, SIZE{}, nullptr, nullptr);
    tracker.last_rect = draw;
    tracker.last_size = size;
}

void RectTrackerAdjustRect(MfcRectTrackerCompat& tracker, int handle) {
    if (handle == 8) {
        return;
    }
    LONG* x_ptr = nullptr;
    LONG* y_ptr = nullptr;
    LONG fixed_x = 0;
    LONG fixed_y = 0;
    RectTrackerGetModifyPointers(tracker, handle, &x_ptr, &y_ptr,
        &fixed_x, &fixed_y);
    if (x_ptr != nullptr && std::abs(*x_ptr - fixed_x) < tracker.min_size.cx) {
        *x_ptr = fixed_x + (*x_ptr < fixed_x ? -tracker.min_size.cx
                                             : tracker.min_size.cx);
    }
    if (y_ptr != nullptr && std::abs(*y_ptr - fixed_y) < tracker.min_size.cy) {
        *y_ptr = fixed_y + (*y_ptr < fixed_y ? -tracker.min_size.cy
                                             : tracker.min_size.cy);
    }
}

void RectTrackerGetTrueRect(MfcRectTrackerCompat& tracker, RECT& rect) {
    rect = normalized_rect(tracker.rect);
    int inset = 0;
    if ((tracker.style & 0x14U) != 0) {
        inset += RectTrackerGetHandleSize(tracker, &rect) - 1;
    }
    if ((tracker.style & 3U) != 0) {
        ++inset;
    }
    InflateRect(&rect, inset, inset);
}

void RectTrackerOnChangedRect(MfcRectTrackerCompat& tracker) {
    (void)tracker;
}

void RectTrackerGetHandleRect(MfcRectTrackerCompat& tracker, int handle,
    RECT& rect) {
    RECT bounds = normalized_rect(tracker.rect);
    const int size = RectTrackerGetHandleSize(tracker, &bounds);
    const int half = size / 2;
    POINT points[8]{
        {bounds.left, bounds.top},
        {(bounds.left + bounds.right) / 2, bounds.top},
        {bounds.right, bounds.top},
        {bounds.right, (bounds.top + bounds.bottom) / 2},
        {bounds.right, bounds.bottom},
        {(bounds.left + bounds.right) / 2, bounds.bottom},
        {bounds.left, bounds.bottom},
        {bounds.left, (bounds.top + bounds.bottom) / 2}};
    if (handle < 0 || handle > 7) {
        rect = RECT{};
        return;
    }
    rect.left = points[handle].x - half;
    rect.top = points[handle].y - half;
    rect.right = rect.left + size;
    rect.bottom = rect.top + size;
}

int RectTrackerGetHandleSize(MfcRectTrackerCompat& tracker,
    const RECT* rect) {
    int size = tracker.handle_size;
    if ((tracker.style & 0x10U) == 0 && rect != nullptr) {
        const int width = std::abs(rect->right - rect->left);
        const int height = std::abs(rect->bottom - rect->top);
        const int shortest = std::min(width, height);
        if (shortest <= size * 2 && shortest > 0) {
            size = std::max(1, shortest / 2);
        }
    }
    return std::max(1, size);
}

int RectTrackerHitTestHandles(MfcRectTrackerCompat& tracker, POINT point) {
    UINT mask = RectTrackerGetHandleMask(tracker);
    RECT true_rect{};
    RectTrackerGetTrueRect(tracker, true_rect);
    if (!PtInRect(&true_rect, point)) {
        return -1;
    }
    for (int handle = 0; handle < 8; ++handle) {
        if ((mask & (1U << handle)) != 0) {
            RECT handle_rect{};
            RectTrackerGetHandleRect(tracker, handle, handle_rect);
            if (PtInRect(&handle_rect, point)) {
                return handle;
            }
        }
    }
    RECT rect = normalized_rect(tracker.rect);
    return PtInRect(&rect, point) ? 8 : -1;
}

bool RectTrackerTrackHandle(MfcRectTrackerCompat& tracker, int handle,
    MfcCWndCompat& window, POINT point, MfcCWndCompat* clip_window) {
    if (handle < 0 || handle > 8 || window.window == nullptr) {
        return false;
    }
    tracker.clip_window = clip_window;
    tracker.tracking = true;
    LONG* x_ptr = nullptr;
    LONG* y_ptr = nullptr;
    LONG fixed_x = 0;
    LONG fixed_y = 0;
    RectTrackerGetModifyPointers(tracker, handle, &x_ptr, &y_ptr,
        &fixed_x, &fixed_y);
    if (handle == 8) {
        OffsetRect(&tracker.rect, point.x - tracker.rect.left,
            point.y - tracker.rect.top);
    } else {
        if (x_ptr != nullptr) {
            *x_ptr = point.x;
        }
        if (y_ptr != nullptr) {
            *y_ptr = point.y;
        }
        RectTrackerAdjustRect(tracker, handle);
    }
    RectTrackerOnChangedRect(tracker);
    tracker.tracking = false;
    return true;
}

void RectTrackerGetModifyPointers(MfcRectTrackerCompat& tracker, int handle,
    LONG** x_ptr, LONG** y_ptr, LONG* fixed_x, LONG* fixed_y) {
    if (x_ptr != nullptr) {
        *x_ptr = nullptr;
    }
    if (y_ptr != nullptr) {
        *y_ptr = nullptr;
    }
    if (handle == 8) {
        return;
    }
    RECT& rect = tracker.rect;
    LONG* horizontal[8]{&rect.left, nullptr, &rect.right, &rect.right,
        &rect.right, nullptr, &rect.left, &rect.left};
    LONG* vertical[8]{&rect.top, &rect.top, &rect.top, nullptr,
        &rect.bottom, &rect.bottom, &rect.bottom, nullptr};
    LONG fixed_horizontal[8]{rect.right, (rect.left + rect.right) / 2,
        rect.left, rect.left, rect.left, (rect.left + rect.right) / 2,
        rect.right, rect.right};
    LONG fixed_vertical[8]{rect.bottom, rect.bottom, rect.bottom,
        (rect.top + rect.bottom) / 2, rect.top, rect.top, rect.top,
        (rect.top + rect.bottom) / 2};
    if (handle < 0 || handle > 7) {
        return;
    }
    if (x_ptr != nullptr) {
        *x_ptr = horizontal[handle];
    }
    if (y_ptr != nullptr) {
        *y_ptr = vertical[handle];
    }
    if (fixed_x != nullptr) {
        *fixed_x = fixed_horizontal[handle];
    }
    if (fixed_y != nullptr) {
        *fixed_y = fixed_vertical[handle];
    }
}

UINT RectTrackerGetHandleMask(MfcRectTrackerCompat& tracker) {
    UINT mask = 0x0f;
    const int size = tracker.handle_size;
    if (std::abs(rect_tracker_width(tracker.rect)) > size * 3 + 4) {
        mask |= 0x50;
    }
    if (std::abs(rect_tracker_height(tracker.rect)) > size * 3 + 4) {
        mask |= 0xa0;
    }
    return mask;
}

void ClientDCAssertValid(const MfcWindowDCCompat& dc) {
    CDCAssertValid(dc);
    if (dc.window != nullptr && !IsWindow(dc.window)) {
        CDCTraceInvalidWindow("CClientDC", dc.window);
    }
}

MfcWindowDCCompat& ConstructClientDC(MfcWindowDCCompat& dc, HWND window) {
    CDCResetWindowStorage(dc);
    if (window != nullptr && !IsWindow(window)) {
        CDCTraceInvalidWindow("CClientDC", window);
    }
    dc.window = window;
    HDC handle = GetDC(dc.window);
    if (!CDCAttach(dc, handle)) {
        if (handle != nullptr) {
            ReleaseDC(dc.window, handle);
        }
        ThrowMfcResourceException();
    }
    return dc;
}

void DestroyClientDC(MfcWindowDCCompat& dc) {
    if (dc.output_dc == nullptr) {
        AfxTraceOutput("CClientDC destroyed without an attached HDC\n");
    }
    HDC handle = CDCDetachHandle(dc);
    if (handle != nullptr) {
        ReleaseDC(dc.window, handle);
    }
    dc.window = nullptr;
    std::memset(&dc.paint, 0, sizeof(dc.paint));
}

void WindowDCAssertValid(const MfcWindowDCCompat& dc) {
    CDCAssertValid(dc);
    if (dc.window != nullptr && !IsWindow(dc.window)) {
        CDCTraceInvalidWindow("CWindowDC", dc.window);
    }
}

MfcWindowDCCompat& ConstructWindowDC(MfcWindowDCCompat& dc, HWND window) {
    CDCResetWindowStorage(dc);
    if (window != nullptr && !IsWindow(window)) {
        CDCTraceInvalidWindow("CWindowDC", window);
    }
    dc.window = window;
    HDC handle = GetWindowDC(dc.window);
    if (!CDCAttach(dc, handle)) {
        if (handle != nullptr) {
            ReleaseDC(dc.window, handle);
        }
        ThrowMfcResourceException();
    }
    return dc;
}

void DestroyWindowDC(MfcWindowDCCompat& dc) {
    if (dc.output_dc == nullptr) {
        AfxTraceOutput("CWindowDC destroyed without an attached HDC\n");
    }
    HDC handle = CDCDetachHandle(dc);
    if (handle != nullptr) {
        ReleaseDC(dc.window, handle);
    }
    dc.window = nullptr;
    std::memset(&dc.paint, 0, sizeof(dc.paint));
}

void PaintDCAssertValid(const MfcWindowDCCompat& dc) {
    CDCAssertValid(dc);
    if (dc.window == nullptr || !IsWindow(dc.window)) {
        CDCTraceInvalidWindow("CPaintDC", dc.window);
    }
}

MfcWindowDCCompat& ConstructPaintDC(MfcWindowDCCompat& dc, HWND window) {
    CDCResetWindowStorage(dc);
    if (window == nullptr || !IsWindow(window)) {
        CDCTraceInvalidWindow("CPaintDC", window);
    }
    dc.window = window;
    HDC handle = BeginPaint(dc.window, &dc.paint);
    if (!CDCAttach(dc, handle)) {
        ThrowMfcResourceException();
    }
    return dc;
}

void DestroyPaintDC(MfcWindowDCCompat& dc) {
    if (dc.output_dc == nullptr) {
        AfxTraceOutput("CPaintDC destroyed without an attached HDC\n");
    }
    if (dc.window != nullptr && IsWindow(dc.window)) {
        EndPaint(dc.window, &dc.paint);
    } else {
        CDCTraceInvalidWindow("CPaintDC", dc.window);
    }
    CDCDetachHandle(dc);
    dc.window = nullptr;
    std::memset(&dc.paint, 0, sizeof(dc.paint));
}

void DestroyCDC(MfcCDCCompat& dc) {
    HDC handle = CDCDetachHandle(dc);
    if (handle != nullptr) {
        DeleteDC(handle);
    }
    dc.runtime_class = nullptr;
}

void CDCDestructor(MfcCDCCompat& dc) {
    DestroyCDC(dc);
}

MfcCDCCompat* DeleteCDCScalarDtor(MfcCDCCompat* dc, unsigned flags) {
    if (dc == nullptr) {
        return nullptr;
    }
    DestroyCDC(*dc);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dc);
    }
    return dc;
}

MfcWindowDCCompat* DeleteClientDCScalarDtor(MfcWindowDCCompat* dc,
    unsigned flags) {
    if (dc == nullptr) {
        return nullptr;
    }
    DestroyClientDC(*dc);
    dc->runtime_class = nullptr;
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dc);
    }
    return dc;
}

MfcWindowDCCompat* DeleteWindowDCScalarDtor(MfcWindowDCCompat* dc,
    unsigned flags) {
    if (dc == nullptr) {
        return nullptr;
    }
    DestroyWindowDC(*dc);
    dc->runtime_class = nullptr;
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dc);
    }
    return dc;
}

MfcWindowDCCompat* DeletePaintDCScalarDtor(MfcWindowDCCompat* dc,
    unsigned flags) {
    if (dc == nullptr) {
        return nullptr;
    }
    DestroyPaintDC(*dc);
    dc->runtime_class = nullptr;
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(dc);
    }
    return dc;
}

[[noreturn]] void ThrowMfcMemoryExceptionAlias() {
    ThrowMfcMemoryException();
}

void CGdiObjectAssertValid(const MfcGdiObjectCompat& object) {
    CObjectAssertValid(&object);
    if (object.object != nullptr && GetObjectType(object.object) == 0) {
        AfxTraceOutput("CGdiObject has an invalid HGDIOBJ %p\n",
            object.object);
    }
}

MfcHandleMapCompat* GetTempGdiObjectHandleMap(bool create) {
    if (!g_gdi_object_handle_map_created) {
        if (!create) {
            return nullptr;
        }
        ConstructHandleMap(g_gdi_object_handle_map, 0, nullptr, 1);
        g_gdi_object_handle_map_created = true;
    }
    return &g_gdi_object_handle_map;
}

MfcGdiObjectCompat* GdiObjectFromHandle(HGDIOBJ handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    auto* map = GetTempGdiObjectHandleMap(true);
    auto permanent = map->permanent.find(handle);
    if (permanent != map->permanent.end()) {
        return static_cast<MfcGdiObjectCompat*>(permanent->second);
    }
    auto temporary = map->temporary.find(handle);
    if (temporary != map->temporary.end()) {
        return static_cast<MfcGdiObjectCompat*>(temporary->second);
    }

    auto wrapper = std::make_unique<MfcGdiObjectCompat>();
    wrapper->runtime_class = GetCObjectRuntimeClass();
    wrapper->object = handle;
    wrapper->temporary = true;
    MfcGdiObjectCompat* raw = wrapper.get();
    g_temporary_gdi_objects.push_back(std::move(wrapper));
    map->temporary[handle] = raw;
    return raw;
}

bool GdiObjectAttach(MfcGdiObjectCompat& object, HGDIOBJ handle) {
    if (object.object != nullptr || handle == nullptr) {
        return false;
    }
    auto* map = GetTempGdiObjectHandleMap(true);
    if (map->permanent.find(handle) != map->permanent.end()) {
        return false;
    }
    object.runtime_class = GetCObjectRuntimeClass();
    object.object = handle;
    object.temporary = false;
    map->temporary.erase(handle);
    map->permanent[handle] = &object;
    return true;
}

HGDIOBJ GdiObjectDetach(MfcGdiObjectCompat& object) {
    HGDIOBJ handle = object.object;
    if (handle != nullptr) {
        if (auto* map = GetTempGdiObjectHandleMap(false)) {
            map->permanent.erase(handle);
            map->temporary.erase(handle);
        }
    }
    object.object = nullptr;
    object.temporary = false;
    return handle;
}

HGDIOBJ Detach(MfcGdiObjectCompat& object) {
    return GdiObjectDetach(object);
}

BOOL GdiObjectDeleteObject(MfcGdiObjectCompat& object) {
    HGDIOBJ handle = GdiObjectDetach(object);
    return handle == nullptr ? FALSE : DeleteObject(handle);
}

namespace {

bool attach_created_gdi_object(MfcGdiObjectCompat& object, HGDIOBJ handle) {
    const bool attached = GdiObjectAttach(object, handle);
    if (!attached && handle != nullptr) {
        DeleteObject(handle);
    }
    return attached;
}

} // namespace

MfcGdiObjectCompat& ConstructGdiObjectCompat(MfcGdiObjectCompat& object) {
    ConstructCObject(object);
    object.object = nullptr;
    object.temporary = false;
    return object;
}

void DestroyGdiObjectCompat(MfcGdiObjectCompat& object) {
    GdiObjectDeleteObject(object);
    DestroyCObject(object);
}

MfcGdiObjectCompat& ConstructPen(MfcGdiObjectCompat& pen) {
    return ConstructGdiObjectCompat(pen);
}

void DestroyPen(MfcGdiObjectCompat& pen) {
    DestroyGdiObjectCompat(pen);
}

MfcGdiObjectCompat& ConstructBrush(MfcGdiObjectCompat& brush) {
    return ConstructGdiObjectCompat(brush);
}

void DestroyBrush(MfcGdiObjectCompat& brush) {
    DestroyGdiObjectCompat(brush);
}

MfcGdiObjectCompat& ConstructFont(MfcGdiObjectCompat& font) {
    return ConstructGdiObjectCompat(font);
}

void DestroyFont(MfcGdiObjectCompat& font) {
    DestroyGdiObjectCompat(font);
}

MfcGdiObjectCompat& ConstructBitmap(MfcGdiObjectCompat& bitmap) {
    return ConstructGdiObjectCompat(bitmap);
}

void DestroyBitmap(MfcGdiObjectCompat& bitmap) {
    DestroyGdiObjectCompat(bitmap);
}

MfcGdiObjectCompat& ConstructPalette(MfcGdiObjectCompat& palette) {
    return ConstructGdiObjectCompat(palette);
}

void DestroyPalette(MfcGdiObjectCompat& palette) {
    DestroyGdiObjectCompat(palette);
}

namespace {

using GdiDestroyFn = void (*)(MfcGdiObjectCompat&);

MfcGdiObjectCompat* DeleteTypedGdiScalarDtor(MfcGdiObjectCompat* object,
    unsigned flags, GdiDestroyFn destroy) {
    if (object == nullptr) {
        return nullptr;
    }
    destroy(*object);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(object);
    }
    return object;
}

} // namespace

MfcGdiObjectCompat* DeletePenScalarDtor(MfcGdiObjectCompat* pen,
    unsigned flags) {
    return DeleteTypedGdiScalarDtor(pen, flags, DestroyPen);
}

MfcGdiObjectCompat* DeleteBrushScalarDtor(MfcGdiObjectCompat* brush,
    unsigned flags) {
    return DeleteTypedGdiScalarDtor(brush, flags, DestroyBrush);
}

MfcGdiObjectCompat* DeleteFontScalarDtor(MfcGdiObjectCompat* font,
    unsigned flags) {
    return DeleteTypedGdiScalarDtor(font, flags, DestroyFont);
}

MfcGdiObjectCompat* DeleteBitmapScalarDtor(MfcGdiObjectCompat* bitmap,
    unsigned flags) {
    return DeleteTypedGdiScalarDtor(bitmap, flags, DestroyBitmap);
}

MfcGdiObjectCompat* DeletePaletteScalarDtor(MfcGdiObjectCompat* palette,
    unsigned flags) {
    return DeleteTypedGdiScalarDtor(palette, flags, DestroyPalette);
}

MfcGdiObjectCompat* DeleteRgnScalarDtor(MfcGdiObjectCompat* region,
    unsigned flags) {
    return DeleteTypedGdiScalarDtor(region, flags, DestroyRgn);
}

int GdiObjectGetObjectBytes(MfcGdiObjectCompat& object, int bytes,
    void* buffer) {
    if (object.object == nullptr) {
        return 0;
    }
    return GetObjectA(object.object, bytes, buffer);
}

BOOL GdiObjectUnrealize(MfcGdiObjectCompat& object) {
    return object.object == nullptr ? FALSE : UnrealizeObject(object.object);
}

DWORD GdiObjectGetObjectTypeCompat(const MfcGdiObjectCompat& object) {
    return object.object == nullptr ? 0 : GetObjectType(object.object);
}

HGDIOBJ GdiObjectGetSafeHandle(const MfcGdiObjectCompat* object) {
    return object == nullptr ? nullptr : object->object;
}

MfcGdiObjectCompat* GdiObjectFromHandleCompat(HGDIOBJ handle) {
    return GdiObjectFromHandle(handle);
}

bool PenCreate(MfcGdiObjectCompat& pen, int style, int width,
    COLORREF color) {
    return attach_created_gdi_object(pen, CreatePen(style, width, color));
}

bool PenCreateIndirect(MfcGdiObjectCompat& pen, const LOGPEN& log_pen) {
    return attach_created_gdi_object(pen, CreatePenIndirect(&log_pen));
}

bool PenCreateExt(MfcGdiObjectCompat& pen, DWORD style, DWORD width,
    const LOGBRUSH& brush, DWORD style_count, const DWORD* style_bits) {
    return attach_created_gdi_object(pen,
        ExtCreatePen(style, width, &brush, style_count, style_bits));
}

int PenGetExtLogPen(MfcGdiObjectCompat& pen, EXTLOGPEN& log_pen) {
    return GdiObjectGetObjectBytes(pen, sizeof(log_pen), &log_pen);
}

int PenGetLogPen(MfcGdiObjectCompat& pen, LOGPEN& log_pen) {
    return GdiObjectGetObjectBytes(pen, sizeof(log_pen), &log_pen);
}

HPEN PenGetSafeHandle(const MfcGdiObjectCompat* pen) {
    return static_cast<HPEN>(GdiObjectGetSafeHandle(pen));
}

MfcGdiObjectCompat* PenFromHandle(HPEN pen) {
    return GdiObjectFromHandle(pen);
}

void DumpPenObject(const MfcGdiObjectCompat& pen) {
    CGdiObjectAssertValid(pen);
    if (pen.object == nullptr) {
        return;
    }
    DWORD type = GetObjectType(pen.object);
    if (type != OBJ_PEN && type != OBJ_EXTPEN) {
        AfxTraceOutput("has ILLEGAL HPEN\n");
        return;
    }
    LOGPEN log_pen{};
    if (GetObjectA(pen.object, sizeof(log_pen), &log_pen) == 0) {
        AfxTraceOutput("Unable to query HPEN %p\n", pen.object);
        return;
    }
    AfxTraceOutput("lgpn.lopnStyle = %u\n", log_pen.lopnStyle);
    AfxTraceOutput("lgpn.lopnWidth.x = %ld\n", log_pen.lopnWidth.x);
    AfxTraceOutput("lgpn.lopnColor = 0x%08lx\n", log_pen.lopnColor);
}

bool BrushCreateSolid(MfcGdiObjectCompat& brush, COLORREF color) {
    return attach_created_gdi_object(brush, CreateSolidBrush(color));
}

bool BrushCreateHatch(MfcGdiObjectCompat& brush, int hatch, COLORREF color) {
    return attach_created_gdi_object(brush, CreateHatchBrush(hatch, color));
}

bool BrushCreateIndirect(MfcGdiObjectCompat& brush,
    const LOGBRUSH& log_brush) {
    return attach_created_gdi_object(brush, CreateBrushIndirect(&log_brush));
}

bool BrushCreatePattern(MfcGdiObjectCompat& brush,
    const MfcGdiObjectCompat& bitmap) {
    return attach_created_gdi_object(brush,
        CreatePatternBrush(static_cast<HBITMAP>(bitmap.object)));
}

bool BrushCreateDIBPattern(MfcGdiObjectCompat& brush, const void* packed_dib,
    UINT usage) {
    return attach_created_gdi_object(brush,
        CreateDIBPatternBrushPt(packed_dib, usage));
}

bool BrushCreateSysColor(MfcGdiObjectCompat& brush, int color_index) {
    return GdiObjectAttach(brush, GetSysColorBrush(color_index));
}

bool BrushCreateDIBPatternFromGlobal(MfcGdiObjectCompat& brush,
    HGLOBAL global_dib, UINT usage) {
    if (global_dib == nullptr) {
        return false;
    }
    void* packed_dib = GlobalLock(global_dib);
    if (packed_dib == nullptr) {
        return false;
    }
    HBRUSH handle = CreateDIBPatternBrushPt(packed_dib, usage);
    const bool attached = GdiObjectAttach(brush, handle);
    if (!attached && handle != nullptr) {
        DeleteObject(handle);
    }
    GlobalUnlock(global_dib);
    return attached;
}

int BrushGetLogBrush(MfcGdiObjectCompat& brush, LOGBRUSH& log_brush) {
    return GdiObjectGetObjectBytes(brush, sizeof(log_brush), &log_brush);
}

HBRUSH BrushGetSafeHandle(const MfcGdiObjectCompat* brush) {
    return static_cast<HBRUSH>(GdiObjectGetSafeHandle(brush));
}

MfcGdiObjectCompat* BrushFromHandle(HBRUSH brush) {
    return GdiObjectFromHandle(brush);
}

void DumpBrushObject(const MfcGdiObjectCompat& brush) {
    CGdiObjectAssertValid(brush);
    if (brush.object == nullptr) {
        return;
    }
    if (GetObjectType(brush.object) != OBJ_BRUSH) {
        AfxTraceOutput("has ILLEGAL HBRUSH\n");
        return;
    }
    LOGBRUSH log_brush{};
    if (GetObjectA(brush.object, sizeof(log_brush), &log_brush) == 0) {
        AfxTraceOutput("Unable to query HBRUSH %p\n", brush.object);
        return;
    }
    AfxTraceOutput("lb.lbStyle = %u\n", log_brush.lbStyle);
    AfxTraceOutput("lb.lbHatch = %p\n",
        reinterpret_cast<void*>(log_brush.lbHatch));
    AfxTraceOutput("lb.lbColor = 0x%08lx\n", log_brush.lbColor);
}

bool FontCreateIndirect(MfcGdiObjectCompat& font, const LOGFONTA& log_font) {
    return attach_created_gdi_object(font, CreateFontIndirectA(&log_font));
}

bool FontCreate(MfcGdiObjectCompat& font, int height, int width,
    int escapement, int orientation, int weight, BYTE italic,
    BYTE underline, BYTE strike_out, BYTE char_set, BYTE output_precision,
    BYTE clip_precision, BYTE quality, BYTE pitch_and_family,
    const char* face_name) {
    return attach_created_gdi_object(font,
        CreateFontA(height, width, escapement, orientation, weight, italic,
            underline, strike_out, char_set, output_precision, clip_precision,
            quality, pitch_and_family, face_name));
}

int FontGetLogFont(MfcGdiObjectCompat& font, LOGFONTA& log_font) {
    return GdiObjectGetObjectBytes(font, sizeof(log_font), &log_font);
}

HFONT FontGetSafeHandle(const MfcGdiObjectCompat* font) {
    return static_cast<HFONT>(GdiObjectGetSafeHandle(font));
}

MfcGdiObjectCompat* FontFromHandle(HFONT font) {
    return GdiObjectFromHandle(font);
}

void DumpFontObject(const MfcGdiObjectCompat& font) {
    CGdiObjectAssertValid(font);
    if (font.object == nullptr) {
        return;
    }
    if (GetObjectType(font.object) != OBJ_FONT) {
        AfxTraceOutput("has ILLEGAL HFONT\n");
        return;
    }
    LOGFONTA log_font{};
    if (GetObjectA(font.object, sizeof(log_font), &log_font) == 0) {
        AfxTraceOutput("Unable to query HFONT %p\n", font.object);
        return;
    }
    AfxTraceOutput("lf.lfHeight = %ld\n", log_font.lfHeight);
    AfxTraceOutput("lf.lfWidth = %ld\n", log_font.lfWidth);
    AfxTraceOutput("lf.lfEscapement = %ld\n", log_font.lfEscapement);
    AfxTraceOutput("lf.lfOrientation = %ld\n", log_font.lfOrientation);
    AfxTraceOutput("lf.lfWeight = %ld\n", log_font.lfWeight);
    AfxTraceOutput("lf.lfItalic = %u\n", log_font.lfItalic);
    AfxTraceOutput("lf.lfUnderline = %u\n", log_font.lfUnderline);
    AfxTraceOutput("lf.lfStrikeOut = %u\n", log_font.lfStrikeOut);
    AfxTraceOutput("lf.lfCharSet = %u\n", log_font.lfCharSet);
    AfxTraceOutput("lf.lfOutPrecision = %u\n", log_font.lfOutPrecision);
    AfxTraceOutput("lf.lfClipPrecision = %u\n", log_font.lfClipPrecision);
    AfxTraceOutput("lf.lfQuality = %u\n", log_font.lfQuality);
    AfxTraceOutput("lf.lfPitchAndFamily = %u\n", log_font.lfPitchAndFamily);
    AfxTraceOutput("lf.lfFaceName = %s\n", log_font.lfFaceName);
}

bool BitmapCreate(MfcGdiObjectCompat& bitmap, int width, int height,
    UINT planes, UINT bit_count, const void* bits) {
    return attach_created_gdi_object(bitmap,
        CreateBitmap(width, height, planes, bit_count, bits));
}

bool BitmapCreateIndirect(MfcGdiObjectCompat& bitmap, const BITMAP& info) {
    return attach_created_gdi_object(bitmap, CreateBitmapIndirect(&info));
}

LONG BitmapSetBits(MfcGdiObjectCompat& bitmap, DWORD bytes, const void* bits) {
    return bitmap.object == nullptr ? 0
        : SetBitmapBits(static_cast<HBITMAP>(bitmap.object), bytes, bits);
}

LONG BitmapGetBits(MfcGdiObjectCompat& bitmap, LONG bytes, void* bits) {
    return bitmap.object == nullptr ? 0
        : GetBitmapBits(static_cast<HBITMAP>(bitmap.object), bytes, bits);
}

bool BitmapLoadResource(MfcGdiObjectCompat& bitmap, const char* name) {
    return attach_created_gdi_object(bitmap,
        LoadBitmapA(AfxGetResourceHandleCompat(), name));
}

bool BitmapLoadResourceId(MfcGdiObjectCompat& bitmap, UINT id) {
    return BitmapLoadResource(bitmap, MAKEINTRESOURCEA(id));
}

bool BitmapLoadMappedResource(MfcGdiObjectCompat& bitmap, INT_PTR id,
    UINT flags, LPCOLORMAP color_map, int color_count) {
    return attach_created_gdi_object(bitmap,
        CreateMappedBitmap(AfxGetResourceHandleCompat(), id, flags,
            color_map, color_count));
}

bool BitmapLoadOEM(MfcGdiObjectCompat& bitmap, UINT id) {
    return attach_created_gdi_object(bitmap,
        LoadBitmapA(nullptr, MAKEINTRESOURCEA(id)));
}

bool BitmapCreateCompatible(MfcGdiObjectCompat& bitmap, HDC dc, int width,
    int height) {
    return attach_created_gdi_object(bitmap,
        CreateCompatibleBitmap(dc, width, height));
}

bool BitmapCreateDiscardable(MfcGdiObjectCompat& bitmap, HDC dc, int width,
    int height) {
    return attach_created_gdi_object(bitmap,
        CreateDiscardableBitmap(dc, width, height));
}

SIZE BitmapSetDimension(MfcGdiObjectCompat& bitmap, int width, int height) {
    SIZE previous{};
    if (bitmap.object != nullptr) {
        SetBitmapDimensionEx(static_cast<HBITMAP>(bitmap.object), width, height,
            &previous);
    }
    return previous;
}

SIZE BitmapGetDimension(MfcGdiObjectCompat& bitmap) {
    SIZE size{};
    if (bitmap.object != nullptr) {
        GetBitmapDimensionEx(static_cast<HBITMAP>(bitmap.object), &size);
    }
    return size;
}

int BitmapGetObject(MfcGdiObjectCompat& bitmap, BITMAP& info) {
    return GdiObjectGetObjectBytes(bitmap, sizeof(info), &info);
}

HBITMAP BitmapGetSafeHandle(const MfcGdiObjectCompat* bitmap) {
    return static_cast<HBITMAP>(GdiObjectGetSafeHandle(bitmap));
}

MfcGdiObjectCompat* BitmapFromHandle(HBITMAP bitmap) {
    return GdiObjectFromHandle(bitmap);
}

bool PaletteCreate(MfcGdiObjectCompat& palette,
    const LOGPALETTE& log_palette) {
    return attach_created_gdi_object(palette, CreatePalette(&log_palette));
}

bool PaletteCreateHalftone(MfcGdiObjectCompat& palette, HDC dc) {
    return attach_created_gdi_object(palette, CreateHalftonePalette(dc));
}

UINT PaletteGetEntries(MfcGdiObjectCompat& palette, UINT start, UINT count,
    PALETTEENTRY* entries) {
    return palette.object == nullptr ? 0
        : GetPaletteEntries(static_cast<HPALETTE>(palette.object), start,
            count, entries);
}

UINT PaletteSetEntries(MfcGdiObjectCompat& palette, UINT start, UINT count,
    const PALETTEENTRY* entries) {
    return palette.object == nullptr ? 0
        : SetPaletteEntries(static_cast<HPALETTE>(palette.object), start,
            count, entries);
}

void PaletteAnimate(MfcGdiObjectCompat& palette, UINT start, UINT count,
    const PALETTEENTRY* entries) {
    if (palette.object != nullptr) {
        AnimatePalette(static_cast<HPALETTE>(palette.object), start, count,
            entries);
    }
}

UINT PaletteGetNearestIndex(MfcGdiObjectCompat& palette, COLORREF color) {
    return palette.object == nullptr ? CLR_INVALID
        : GetNearestPaletteIndex(static_cast<HPALETTE>(palette.object), color);
}

BOOL PaletteResize(MfcGdiObjectCompat& palette, UINT count) {
    return palette.object == nullptr ? FALSE
        : ResizePalette(static_cast<HPALETTE>(palette.object), count);
}

UINT PaletteGetEntryCount(MfcGdiObjectCompat& palette) {
    WORD count = 0;
    if (palette.object != nullptr) {
        GetObjectA(palette.object, sizeof(count), &count);
    }
    return count;
}

HPALETTE PaletteGetSafeHandle(const MfcGdiObjectCompat* palette) {
    return static_cast<HPALETTE>(GdiObjectGetSafeHandle(palette));
}

MfcGdiObjectCompat* PaletteFromHandle(HPALETTE palette) {
    return GdiObjectFromHandle(palette);
}

MfcGdiObjectCompat& ConstructRgn(MfcGdiObjectCompat& region) {
    return ConstructGdiObjectCompat(region);
}

void DestroyRgn(MfcGdiObjectCompat& region) {
    DestroyGdiObjectCompat(region);
}

bool RgnCreateRect(MfcGdiObjectCompat& region, int left, int top,
    int right, int bottom) {
    return attach_created_gdi_object(region,
        CreateRectRgn(left, top, right, bottom));
}

bool RgnCreateRectIndirect(MfcGdiObjectCompat& region, const RECT& rect) {
    return attach_created_gdi_object(region, CreateRectRgnIndirect(&rect));
}

bool RgnCreateElliptic(MfcGdiObjectCompat& region, int left, int top,
    int right, int bottom) {
    return attach_created_gdi_object(region,
        CreateEllipticRgn(left, top, right, bottom));
}

bool RgnCreateEllipticIndirect(MfcGdiObjectCompat& region, const RECT& rect) {
    return attach_created_gdi_object(region, CreateEllipticRgnIndirect(&rect));
}

bool RgnCreatePolygon(MfcGdiObjectCompat& region, const POINT* points,
    int count, int mode) {
    return attach_created_gdi_object(region,
        CreatePolygonRgn(points, count, mode));
}

bool RgnCreatePolyPolygon(MfcGdiObjectCompat& region, const POINT* points,
    const INT* counts, int polygon_count, int mode) {
    return attach_created_gdi_object(region,
        CreatePolyPolygonRgn(points, counts, polygon_count, mode));
}

bool RgnCreateRoundRect(MfcGdiObjectCompat& region, int left, int top,
    int right, int bottom, int ellipse_width, int ellipse_height) {
    return attach_created_gdi_object(region,
        CreateRoundRectRgn(left, top, right, bottom, ellipse_width,
            ellipse_height));
}

bool RgnCreateFromPath(MfcGdiObjectCompat& region, HDC dc) {
    return attach_created_gdi_object(region, PathToRegion(dc));
}

bool RgnCreateExt(MfcGdiObjectCompat& region, const XFORM* transform,
    DWORD bytes, const RGNDATA* data) {
    return attach_created_gdi_object(region,
        ExtCreateRegion(transform, bytes, data));
}

DWORD RgnGetRegionData(MfcGdiObjectCompat& region, DWORD bytes,
    RGNDATA* data) {
    return region.object == nullptr ? 0
        : GetRegionData(static_cast<HRGN>(region.object), bytes, data);
}

BOOL RgnSetRect(MfcGdiObjectCompat& region, int left, int top, int right,
    int bottom) {
    return region.object == nullptr ? FALSE
        : SetRectRgn(static_cast<HRGN>(region.object), left, top, right,
            bottom);
}

BOOL RgnSetRectIndirect(MfcGdiObjectCompat& region, const RECT& rect) {
    return RgnSetRect(region, rect.left, rect.top, rect.right, rect.bottom);
}

int RgnCombine(MfcGdiObjectCompat& region, const MfcGdiObjectCompat* left,
    const MfcGdiObjectCompat* right, int mode) {
    if (region.object == nullptr) {
        return ERROR;
    }
    return CombineRgn(static_cast<HRGN>(region.object),
        static_cast<HRGN>(GdiObjectGetSafeHandle(left)),
        static_cast<HRGN>(GdiObjectGetSafeHandle(right)), mode);
}

int RgnCopy(MfcGdiObjectCompat& region, const MfcGdiObjectCompat& source) {
    return RgnCombine(region, &source, nullptr, RGN_COPY);
}

BOOL RgnEqual(MfcGdiObjectCompat& region, const MfcGdiObjectCompat& other) {
    return region.object == nullptr ? FALSE
        : EqualRgn(static_cast<HRGN>(region.object),
            static_cast<HRGN>(other.object));
}

int RgnOffsetXY(MfcGdiObjectCompat& region, int x, int y) {
    return region.object == nullptr ? ERROR
        : OffsetRgn(static_cast<HRGN>(region.object), x, y);
}

int RgnOffsetPoint(MfcGdiObjectCompat& region, POINT point) {
    return RgnOffsetXY(region, point.x, point.y);
}

int RgnGetBox(MfcGdiObjectCompat& region, RECT& rect) {
    return region.object == nullptr ? ERROR
        : GetRgnBox(static_cast<HRGN>(region.object), &rect);
}

BOOL RgnPtInXY(MfcGdiObjectCompat& region, int x, int y) {
    return region.object == nullptr ? FALSE
        : PtInRegion(static_cast<HRGN>(region.object), x, y);
}

BOOL RgnPtInPoint(MfcGdiObjectCompat& region, POINT point) {
    return RgnPtInXY(region, point.x, point.y);
}

BOOL RgnRectIn(MfcGdiObjectCompat& region, const RECT& rect) {
    return region.object == nullptr ? FALSE
        : RectInRegion(static_cast<HRGN>(region.object), &rect);
}

HRGN RgnGetSafeHandle(const MfcGdiObjectCompat* region) {
    return static_cast<HRGN>(GdiObjectGetSafeHandle(region));
}

MfcGdiObjectCompat* RgnFromHandle(HRGN region) {
    return GdiObjectFromHandle(region);
}

void DumpBitmapObject(const MfcGdiObjectCompat& bitmap) {
    CGdiObjectAssertValid(bitmap);
    if (bitmap.object == nullptr) {
        return;
    }
    if (GetObjectType(bitmap.object) != OBJ_BITMAP) {
        AfxTraceOutput("has ILLEGAL HBITMAP\n");
        return;
    }
    BITMAP info{};
    if (GetObjectA(bitmap.object, sizeof(info), &info) == 0) {
        AfxTraceOutput("Unable to query HBITMAP %p\n", bitmap.object);
        return;
    }
    AfxTraceOutput("bm.bmType = %ld\n", info.bmType);
    AfxTraceOutput("bm.bmHeight = %ld\n", info.bmHeight);
    AfxTraceOutput("bm.bmWidth = %ld\n", info.bmWidth);
    AfxTraceOutput("bm.bmWidthBytes = %ld\n", info.bmWidthBytes);
    AfxTraceOutput("bm.bmPlanes = %u\n", info.bmPlanes);
    AfxTraceOutput("bm.bmBitsPixel = %u\n", info.bmBitsPixel);
}

const char* GetCommonDialogRuntimeClassName() {
    return "CCommonDialog";
}

UINT_PTR CALLBACK MfcCommonDialogHookProc(HWND dialog, UINT message, WPARAM wparam,
    LPARAM lparam) {
    static UINT msg_lb_sel_changed = 0;
    static UINT msg_share_violation = 0;
    static UINT msg_file_name_ok = 0;
    static UINT msg_color_ok = 0;
    static UINT msg_help = 0;
    static UINT msg_set_rgb_color = 0;

    if (dialog == nullptr) {
        return 0;
    }
    if (message == WM_INITDIALOG) {
        msg_lb_sel_changed = RegisterWindowMessageA("commdlg_LBSelChangedNotify");
        msg_share_violation = RegisterWindowMessageA("commdlg_ShareViolation");
        msg_file_name_ok = RegisterWindowMessageA("commdlg_FileNameOK");
        msg_color_ok = RegisterWindowMessageA("commdlg_ColorOK");
        msg_help = RegisterWindowMessageA("commdlg_help");
        msg_set_rgb_color = RegisterWindowMessageA("commdlg_SetRGBColor");
        (void)msg_lb_sel_changed;
        (void)msg_share_violation;
        (void)msg_file_name_ok;
        (void)msg_color_ok;
        (void)msg_set_rgb_color;
        return 0;
    }
    if (message == msg_help ||
        (message == WM_COMMAND && LOWORD(wparam) == kCommonDialogHelpButton)) {
        SendMessageA(dialog, WM_COMMAND, kMfcHelpCommand, 0);
        return 1;
    }
    (void)lparam;
    return 0;
}

void DialogOnInitDoneUpdateData(HWND dialog) {
    if (dialog == nullptr) {
        return;
    }
    if (SendMessageA(dialog, WM_INITDIALOG, 0, 0) == 0) {
        AfxTraceOutput("UpdateData failed during dialog termination.\n");
    }
}

void DialogOnHelpCommandDefault(HWND dialog) {
    if (dialog != nullptr) {
        DefWindowProcA(dialog, WM_COMMAND, kMfcHelpCommand, 0);
    }
}

void DialogDefaultMessageHandler(HWND dialog) {
    if (dialog != nullptr) {
        DefWindowProcA(dialog, WM_NULL, 0, 0);
    }
}

MfcFileDialogCompat& ConstructMfcFileDialog(MfcFileDialogCompat& dialog,
    bool open_dialog, const char* default_extension, const char* initial_file,
    DWORD flags, const char* filter, HWND owner) {
    dialog = MfcFileDialogCompat{};
    dialog.open_dialog = open_dialog;
    dialog.owner = owner;
    dialog.default_extension = default_extension == nullptr ? "" : default_extension;
    dialog.filter_storage = filter == nullptr ? "" : filter;
    for (char& ch : dialog.filter_storage) {
        if (ch == '|') {
            ch = '\0';
        }
    }
    if (initial_file != nullptr) {
        lstrcpynA(dialog.file.data(), initial_file, static_cast<int>(dialog.file.size()));
    }

    dialog.ofn.lStructSize = sizeof(dialog.ofn);
    dialog.ofn.hwndOwner = owner;
    dialog.ofn.lpstrFile = dialog.file.data();
    dialog.ofn.nMaxFile = static_cast<DWORD>(dialog.file.size());
    dialog.ofn.lpstrFileTitle = dialog.file_title.data();
    dialog.ofn.nMaxFileTitle = static_cast<DWORD>(dialog.file_title.size());
    dialog.ofn.lpstrDefExt = dialog.default_extension.empty()
        ? nullptr : dialog.default_extension.c_str();
    dialog.ofn.lpstrFilter = dialog.filter_storage.empty()
        ? nullptr : dialog.filter_storage.c_str();
    dialog.ofn.Flags = flags | OFN_ENABLEHOOK | OFN_EXPLORER;
    dialog.ofn.lpfnHook = MfcCommonDialogHookProc;
    return dialog;
}

int DoModalMfcFileDialog(MfcFileDialogCompat& dialog) {
    if (dialog.ofn.lpstrFile == nullptr ||
        !ValidateMemoryPointer(dialog.ofn.lpstrFile, dialog.ofn.nMaxFile, true)) {
        return 0;
    }
    const BOOL ok = dialog.open_dialog
        ? GetOpenFileNameA(&dialog.ofn)
        : GetSaveFileNameA(&dialog.ofn);
    return ok == FALSE ? 2 : 1;
}

std::string GetMfcFileDialogPathName(const MfcFileDialogCompat& dialog) {
    return dialog.file.data();
}

std::string GetMfcFileDialogFileTitle(const MfcFileDialogCompat& dialog) {
    if (dialog.file_title[0] != '\0') {
        return dialog.file_title.data();
    }
    const char* slash = std::strrchr(dialog.file.data(), '\\');
    const char* alt = std::strrchr(dialog.file.data(), '/');
    const char* base = slash != nullptr ? slash + 1 : dialog.file.data();
    if (alt != nullptr && alt + 1 > base) {
        base = alt + 1;
    }
    return base;
}

std::string GetMfcFileDialogFileName(const MfcFileDialogCompat& dialog) {
    std::string title = GetMfcFileDialogFileTitle(dialog);
    const std::size_t dot = title.find_last_of('.');
    return dot == std::string::npos ? title : title.substr(0, dot);
}

std::string GetMfcFileDialogFileExt(const MfcFileDialogCompat& dialog) {
    std::string title = GetMfcFileDialogFileTitle(dialog);
    const std::size_t dot = title.find_last_of('.');
    return dot == std::string::npos ? std::string{} : title.substr(dot + 1);
}

std::string GetNextMfcFileDialogPathName(MfcFileDialogCompat& dialog,
    const char*& position) {
    const char separator = (dialog.ofn.Flags & OFN_EXPLORER) != 0 ? '\0' : ' ';
    const char* cursor = position != nullptr ? position : dialog.file.data();
    if (cursor == nullptr || *cursor == '\0') {
        position = nullptr;
        return dialog.file.data();
    }

    std::string first = cursor;
    cursor += first.size() + 1;
    if (separator == ' ' && *cursor == '\0') {
        position = nullptr;
        return first;
    }
    if (*cursor == '\0') {
        position = nullptr;
        return first;
    }

    std::string second = cursor;
    cursor += second.size() + 1;
    position = *cursor == '\0' ? nullptr : cursor;
    if (!first.empty() && first.back() != '\\' && first.back() != '/') {
        first.push_back('\\');
    }
    return first + second;
}

void SetMfcFileDialogTemplate(MfcFileDialogCompat& dialog, unsigned old_template,
    unsigned new_template) {
    dialog.template_id = (dialog.ofn.Flags & OFN_EXPLORER) != 0
        ? new_template : old_template;
    dialog.ofn.Flags |= OFN_ENABLETEMPLATE;
    dialog.ofn.lpTemplateName = MAKEINTRESOURCEA(dialog.template_id);
}

void OnFileDialogFolderChange(MfcFileDialogCompat& dialog) {
    if (dialog.owner != nullptr) {
        SendMessageA(dialog.owner, CDM_GETFOLDERPATH, dialog.file.size(),
            reinterpret_cast<LPARAM>(dialog.file.data()));
    }
}

void SetFileDialogControlText(MfcFileDialogCompat& dialog, int control_id,
    const char* text) {
    if (dialog.owner != nullptr) {
        SendMessageA(dialog.owner, CDM_SETCONTROLTEXT, control_id,
            reinterpret_cast<LPARAM>(text == nullptr ? "" : text));
    }
}

void HideFileDialogControl(MfcFileDialogCompat& dialog, int control_id) {
    if (dialog.owner != nullptr) {
        SendMessageA(dialog.owner, CDM_HIDECONTROL, control_id, 0);
    }
}

void SetFileDialogDefaultExtension(MfcFileDialogCompat& dialog, const char* extension) {
    dialog.default_extension = extension == nullptr ? "" : extension;
    dialog.ofn.lpstrDefExt = dialog.default_extension.empty()
        ? nullptr : dialog.default_extension.c_str();
    if (dialog.owner != nullptr) {
        SendMessageA(dialog.owner, CDM_SETDEFEXT, 0,
            reinterpret_cast<LPARAM>(dialog.ofn.lpstrDefExt));
    }
}

unsigned OnFileDialogShareViolation(MfcFileDialogCompat& dialog, const char* path) {
    (void)dialog;
    (void)path;
    return OFN_SHAREWARN;
}

unsigned OnFileDialogFileNameOK(MfcFileDialogCompat& dialog) {
    (void)dialog;
    return 0;
}

void OnFileDialogFolderChangeNotify(MfcFileDialogCompat& dialog) {
    OnFileDialogFolderChange(dialog);
}

void OnFileDialogHelp(MfcFileDialogCompat& dialog) {
    if (dialog.owner != nullptr) {
        SendMessageA(dialog.owner, WM_COMMAND, kMfcHelpCommand, 0);
    }
}

void OnFileDialogTypeChange(MfcFileDialogCompat& dialog) {
    (void)dialog;
}

void OnFileDialogInitDone(MfcFileDialogCompat& dialog) {
    (void)dialog;
}

void OnFileDialogListSelectionChanged(MfcFileDialogCompat& dialog) {
    (void)dialog;
}

bool RouteFileDialogNotify(MfcFileDialogCompat& dialog, const OFNOTIFYA& notify,
    LRESULT& result) {
    switch (notify.hdr.code) {
    case CDN_SHAREVIOLATION:
        result = OnFileDialogShareViolation(dialog,
            reinterpret_cast<const char*>(notify.pszFile));
        return true;
    case CDN_FILEOK:
        result = OnFileDialogFileNameOK(dialog);
        return true;
    case CDN_FOLDERCHANGE:
        OnFileDialogFolderChangeNotify(dialog);
        return true;
    case CDN_HELP:
        OnFileDialogHelp(dialog);
        return true;
    case CDN_TYPECHANGE:
        OnFileDialogTypeChange(dialog);
        return true;
    case CDN_INITDONE:
        OnFileDialogInitDone(dialog);
        return true;
    case CDN_SELCHANGE:
        OnFileDialogListSelectionChanged(dialog);
        return true;
    default:
        return false;
    }
}

void DumpMfcFileDialog(const MfcFileDialogCompat& dialog) {
    AfxTraceOutput(dialog.open_dialog ? "File open dialog\n" : "File save dialog\n");
    AfxTraceOutput("m_ofn.hwndOwner = %p\n", dialog.ofn.hwndOwner);
    AfxTraceOutput("m_ofn.nFilterIndex = %u\n", dialog.ofn.nFilterIndex);
    AfxTraceOutput("m_ofn.lpstrFile = %s\n", dialog.ofn.lpstrFile);
    AfxTraceOutput("m_ofn.Flags = 0x%08lx\n", dialog.ofn.Flags);
}

void FileDialogSetTemplateIds(MfcFileDialogCompat& dialog,
    unsigned old_template, unsigned new_template) {
    SetMfcFileDialogTemplate(dialog, old_template, new_template);
}

DWORD PrintDialogExGetResultAction(const MfcPrintDialogExCompat& dialog) {
    return dialog.result_action;
}

bool PrintDialogPrintRange(const MfcPrintDialogCompat& dialog) {
    return (dialog.pd.Flags & PD_PAGENUMS) != 0;
}

bool PrintDialogPrintSelection(const MfcPrintDialogCompat& dialog) {
    return (dialog.pd.Flags & PD_SELECTION) != 0;
}

bool PrintDialogPrintAll(const MfcPrintDialogCompat& dialog) {
    return !PrintDialogPrintRange(dialog) && !PrintDialogPrintSelection(dialog);
}

bool PrintDialogPrintCollate(const MfcPrintDialogCompat& dialog) {
    return (dialog.pd.Flags & PD_COLLATE) != 0;
}

unsigned PrintDialogGetFromPage(const MfcPrintDialogCompat& dialog) {
    return PrintDialogPrintRange(dialog) ? dialog.pd.nFromPage : UINT_MAX;
}

unsigned PrintDialogGetToPage(const MfcPrintDialogCompat& dialog) {
    return PrintDialogPrintRange(dialog) ? dialog.pd.nToPage : UINT_MAX;
}

HDC PrintDialogGetPrinterDC(const MfcPrintDialogCompat& dialog) {
    if ((dialog.pd.Flags & PD_RETURNDC) == 0) {
        return nullptr;
    }
    return dialog.pd.hDC;
}

void PrintInfoSetMinPage(MfcPrintInfoCompat& info, UINT page) {
    if (info.print_dialog != nullptr) {
        info.print_dialog->pd.nMinPage = static_cast<WORD>(page);
    }
}

void PrintInfoSetMaxPage(MfcPrintInfoCompat& info, UINT page) {
    if (info.print_dialog != nullptr) {
        info.print_dialog->pd.nMaxPage = static_cast<WORD>(page);
    }
}

UINT PrintInfoGetMinPage(const MfcPrintInfoCompat& info) {
    return info.print_dialog != nullptr ? info.print_dialog->pd.nMinPage : 0;
}

UINT PrintInfoGetMaxPage(const MfcPrintInfoCompat& info) {
    return info.print_dialog != nullptr ? info.print_dialog->pd.nMaxPage : 0;
}

UINT PrintInfoGetFromPage(const MfcPrintInfoCompat& info) {
    return info.print_dialog != nullptr ? info.print_dialog->pd.nFromPage : 0;
}

UINT PrintInfoGetToPage(const MfcPrintInfoCompat& info) {
    return info.print_dialog != nullptr ? info.print_dialog->pd.nToPage : 0;
}

namespace {

const LOGFONTA& font_dialog_log_font(const MfcFontDialogCompat& dialog) {
    return dialog.cf.lpLogFont != nullptr ? *dialog.cf.lpLogFont
                                          : dialog.log_font;
}

} // namespace

MfcCStringCompat FontDialogGetFaceName(const MfcFontDialogCompat& dialog) {
    MfcCStringCompat result;
    result.text = font_dialog_log_font(dialog).lfFaceName;
    return result;
}

MfcCStringCompat FontDialogGetStyleName(const MfcFontDialogCompat& dialog) {
    MfcCStringCompat result;
    if (dialog.cf.lpszStyle != nullptr) {
        result.text = dialog.cf.lpszStyle;
    } else {
        result.text = dialog.style_name;
    }
    return result;
}

int FontDialogGetSize(const MfcFontDialogCompat& dialog) {
    return dialog.cf.iPointSize;
}

int FontDialogGetWeight(const MfcFontDialogCompat& dialog) {
    return font_dialog_log_font(dialog).lfWeight;
}

bool FontDialogIsItalic(const MfcFontDialogCompat& dialog) {
    return font_dialog_log_font(dialog).lfItalic != 0;
}

bool FontDialogIsStrikeOut(const MfcFontDialogCompat& dialog) {
    return font_dialog_log_font(dialog).lfStrikeOut != 0;
}

bool FontDialogIsBold(const MfcFontDialogCompat& dialog) {
    return font_dialog_log_font(dialog).lfWeight == FW_BOLD;
}

bool FontDialogIsUnderline(const MfcFontDialogCompat& dialog) {
    return font_dialog_log_font(dialog).lfUnderline != 0;
}

COLORREF FontDialogGetColor(const MfcFontDialogCompat& dialog) {
    return dialog.cf.rgbColors;
}

const LOGFONTA* FontDialogGetCurrentFont(const MfcFontDialogCompat& dialog) {
    return &font_dialog_log_font(dialog);
}

bool FindReplaceIsTerminating(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_DIALOGTERM) != 0;
}

MfcCStringCompat FindReplaceGetReplaceString(
    const MfcFindReplaceDialogCompat& dialog) {
    MfcCStringCompat result;
    result.text = dialog.fr.lpstrReplaceWith == nullptr
        ? "" : dialog.fr.lpstrReplaceWith;
    return result;
}

MfcCStringCompat FindReplaceGetFindString(
    const MfcFindReplaceDialogCompat& dialog) {
    MfcCStringCompat result;
    result.text = dialog.fr.lpstrFindWhat == nullptr
        ? "" : dialog.fr.lpstrFindWhat;
    return result;
}

bool FindReplaceSearchDown(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_DOWN) != 0;
}

bool FindReplaceFindNext(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_FINDNEXT) != 0;
}

bool FindReplaceMatchCase(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_MATCHCASE) != 0;
}

bool FindReplaceMatchWholeWord(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_WHOLEWORD) != 0;
}

bool FindReplaceReplaceCurrent(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_REPLACE) != 0;
}

bool FindReplaceReplaceAll(const MfcFindReplaceDialogCompat& dialog) {
    return (dialog.fr.Flags & FR_REPLACEALL) != 0;
}

void DeleteMiniDockFrameWindow(void* window, unsigned flags) {
    if ((flags & 1u) != 0) {
        LocalFree(window);
    }
}

MfcBitmapButtonCompat& ConstructMfcBitmapButton(
    MfcBitmapButtonCompat& button) {
    button = MfcBitmapButtonCompat{};
    return button;
}

namespace {

HBITMAP load_bitmap_resource(int id) {
    if (id == 0) {
        return nullptr;
    }
    return reinterpret_cast<HBITMAP>(LoadImageA(GetModuleHandleA(nullptr),
        MAKEINTRESOURCEA(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
}

void replace_bitmap(HBITMAP& slot, HBITMAP value) {
    if (slot != nullptr) {
        DeleteObject(slot);
    }
    slot = value;
}

} // namespace

void DestroyMfcBitmapButton(MfcBitmapButtonCompat& button) {
    replace_bitmap(button.normal, nullptr);
    replace_bitmap(button.selected, nullptr);
    replace_bitmap(button.focus, nullptr);
    replace_bitmap(button.disabled, nullptr);
    button.window = nullptr;
}

void CBitmapButtonDestructor(MfcBitmapButtonCompat& button) {
    DestroyMfcBitmapButton(button);
}

MfcBitmapButtonCompat* DeleteMfcBitmapButtonScalarDtor(
    MfcBitmapButtonCompat* button, unsigned flags) {
    if (button == nullptr) {
        return nullptr;
    }
    DestroyMfcBitmapButton(*button);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(button);
    }
    return button;
}

bool LoadMfcBitmapButtonBitmaps(MfcBitmapButtonCompat& button, int normal_id,
    int selected_id, int focus_id, int disabled_id) {
    replace_bitmap(button.normal, load_bitmap_resource(normal_id));
    replace_bitmap(button.selected, load_bitmap_resource(selected_id));
    replace_bitmap(button.focus, load_bitmap_resource(focus_id));
    replace_bitmap(button.disabled, load_bitmap_resource(disabled_id));
    if (button.normal == nullptr) {
        AfxTraceOutput("Failed to load bitmap for normal image.\n");
        return false;
    }
    if (selected_id != 0 && button.selected == nullptr) {
        AfxTraceOutput("Failed to load bitmap for selected image.\n");
        return false;
    }
    return true;
}

bool BitmapButtonLoadBitmapsInline(MfcBitmapButtonCompat& button,
    int normal_id, int selected_id, int focus_id, int disabled_id) {
    return LoadMfcBitmapButtonBitmaps(button, normal_id, selected_id,
        focus_id, disabled_id);
}

void SizeMfcBitmapButtonToContent(MfcBitmapButtonCompat& button) {
    if (button.window == nullptr || button.normal == nullptr) {
        return;
    }
    BITMAP bitmap{};
    if (GetObjectA(button.normal, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        SetWindowPos(button.window, nullptr, 0, 0, bitmap.bmWidth, bitmap.bmHeight,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }
}

bool AutoLoadMfcBitmapButton(MfcBitmapButtonCompat& button, HWND owner,
    int base_resource_id) {
    button.window = owner;
    const bool loaded = LoadMfcBitmapButtonBitmaps(button, base_resource_id,
        base_resource_id + 1, base_resource_id + 2, base_resource_id + 3);
    if (loaded) {
        SizeMfcBitmapButtonToContent(button);
    }
    return loaded;
}

void DrawMfcBitmapButton(MfcBitmapButtonCompat& button, const DRAWITEMSTRUCT& item) {
    HBITMAP selected = button.normal;
    if ((item.itemState & ODS_DISABLED) != 0 && button.disabled != nullptr) {
        selected = button.disabled;
    } else if ((item.itemState & ODS_SELECTED) != 0 && button.selected != nullptr) {
        selected = button.selected;
    } else if ((item.itemState & ODS_FOCUS) != 0 && button.focus != nullptr) {
        selected = button.focus;
    }
    if (selected == nullptr || item.hDC == nullptr) {
        return;
    }
    HDC memory_dc = CreateCompatibleDC(item.hDC);
    if (memory_dc == nullptr) {
        return;
    }
    HGDIOBJ previous = SelectObject(memory_dc, selected);
    BITMAP bitmap{};
    GetObjectA(selected, sizeof(bitmap), &bitmap);
    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, bitmap.bmWidth,
        bitmap.bmHeight, memory_dc, 0, 0, SRCCOPY);
    SelectObject(memory_dc, previous);
    DeleteDC(memory_dc);
}

const char* GetColorDialogRuntimeClassName() {
    return "CColorDialog";
}

COLORREF* GetSavedCustomColorsCompat() {
    static std::array<COLORREF, 16> colors = [] {
        std::array<COLORREF, 16> values{};
        values.fill(RGB(255, 255, 255));
        return values;
    }();
    return colors.data();
}

MfcColorDialogCompat& ConstructMfcColorDialog(MfcColorDialogCompat& dialog,
    COLORREF initial_color, DWORD flags, HWND owner) {
    dialog = MfcColorDialogCompat{};
    dialog.owner = owner;
    dialog.color = initial_color;
    dialog.cc.lStructSize = sizeof(dialog.cc);
    dialog.cc.hwndOwner = owner;
    dialog.cc.rgbResult = initial_color;
    dialog.cc.lpCustColors = GetSavedCustomColorsCompat();
    dialog.cc.Flags = flags | CC_ENABLEHOOK;
    if (initial_color != 0) {
        dialog.cc.Flags |= CC_RGBINIT;
    }
    dialog.cc.lpfnHook = MfcCommonDialogHookProc;
    return dialog;
}

int DoModalMfcColorDialog(MfcColorDialogCompat& dialog) {
    const BOOL ok = ChooseColorA(&dialog.cc);
    if (ok != FALSE) {
        dialog.color = dialog.cc.rgbResult;
        return 1;
    }
    return 2;
}

unsigned OnColorDialogColorOK(MfcColorDialogCompat& dialog) {
    (void)dialog;
    return 0;
}

void SetColorDialogCurrentColor(MfcColorDialogCompat& dialog, COLORREF color) {
    dialog.color = color;
    dialog.cc.rgbResult = color;
    if (dialog.owner != nullptr) {
        SendMessageA(dialog.owner, RegisterWindowMessageA("commdlg_SetRGBColor"),
            0, static_cast<LPARAM>(color));
    }
}

void ColorDialogDefaultMessageHandler(HWND dialog) {
    DialogDefaultMessageHandler(dialog);
}

void DumpMfcColorDialog(const MfcColorDialogCompat& dialog) {
    AfxTraceOutput("m_cc.hwndOwner = %p\n", dialog.cc.hwndOwner);
    AfxTraceOutput("m_cc.rgbResult = 0x%08lx\n", dialog.cc.rgbResult);
    AfxTraceOutput("m_cc.Flags = 0x%08lx\n", dialog.cc.Flags);
}

void DeleteMfcNoTrackObjectBase(void* object, unsigned flags) {
    DestroyMfcNoTrackObjectBase(object);
    if ((flags & 1u) != 0) {
        LocalFree(object);
    }
}

void DestroyMfcNoTrackObjectBase(void* object) {
    (void)object;
}

void DeleteMfcDockFrameWindow(void* object, unsigned flags) {
    DestroyMfcDockFrameWindow(object);
    if ((flags & 1u) != 0) {
        LocalFree(object);
    }
}

void DestroyMfcDockFrameWindow(void* object) {
    (void)object;
}
#endif

} // namespace ranker
