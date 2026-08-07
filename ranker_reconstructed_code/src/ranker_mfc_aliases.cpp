// Compatibility entry points for the reconstructed MFC, CRT, and OLE runtime.
// The implementation lives in the corresponding runtime modules.

#include "ranker_unit_commands.h"
#include "ranker_unit_damage.h"
#include "ranker_directx.h"
#include "ranker_gameplay_sound.h"
#include "ranker_link_lobby.h"
#include "ranker_frontend_layout.h"
#include "ranker_map_effects.h"
#include "ranker_memo_window.h"
#include "ranker_crt_runtime.h"
#include "ranker_mfc_runtime.h"
#include "ranker_network.h"
#include "ranker_ole_datetime.h"
#include "ranker_ole_image_data.h"
#include "ranker_ole_variant.h"
#include "ranker_owner_ai.h"
#include "ranker_production_orders.h"
#include "ranker_reliable_packets.h"
#include "ranker_replay_dialogs.h"
#include "ranker_runtime_resources.h"
#include "ranker_setup_data.h"
#include "ranker_sprite_renderer.h"
#include "ranker_trc.h"
#include "ranker_unit_animation.h"
#include "ranker_unit_equipment.h"
#include "ranker_unit_movement.h"
#include "ranker_unit_spatial_index.h"
#include "ranker_win32_compat.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>

#ifdef _WIN32
#include <sys/utime.h>
#endif

#ifdef _WIN32
#ifdef DrawState
#undef DrawState
#endif
#endif

namespace ranker {

void RegisterMilesShutdownAtExit(void (*callback)()) {
    if (callback != nullptr) {
        std::atexit(callback);
    }
}

wchar_t* ocscpy(wchar_t* destination, const wchar_t* source) {
    if (destination == nullptr || source == nullptr) {
        return nullptr;
    }
    const std::size_t bytes = (std::wcslen(source) + 1) * sizeof(wchar_t);
    return static_cast<wchar_t*>(CrtMemMove(destination, source, bytes));
}

void* ConstructMfcObjectBase(void* object) {
    if (object != nullptr) {
        ConstructCObject(*static_cast<MfcObjectCompat*>(object));
    }
    return object;
}

void* DeleteMfcObjectBase(void* object, unsigned flags) {
    ConstructMfcObjectBase(object);
    if ((flags & 1U) != 0U) {
#ifdef _WIN32
        MfcThreadSlotRuntime_005ead15(static_cast<HLOCAL>(object));
#else
        MfcDebugDeleteClientBlock(object);
#endif
    }
    return object;
}

void DestroyMfcObjectBase(void* object) {
    if (object != nullptr) {
        DestroyCObject(*static_cast<MfcObjectCompat*>(object));
    }
}

bool AfxAssertFailedLine(const char*, int);

void* CSimpleList(void* list, int element_size) {
    if (list != nullptr) {
        const std::uint32_t head = 0;
        auto* bytes = static_cast<unsigned char*>(list);
        std::memcpy(bytes, &head, sizeof(head));
        std::memcpy(bytes + 4, &element_size, sizeof(element_size));
    }
    return list;
}

void* InitializeMfcSimpleListElementSize(void* list, int element_size) {
    if (list == nullptr) {
        return nullptr;
    }
    std::uint32_t head = 0;
    auto* bytes = static_cast<unsigned char*>(list);
    std::memcpy(&head, bytes, sizeof(head));
    if (head != 0 &&
        AfxAssertFailedLine("E:\\8665\\vc98\\mfc\\mfc\\include\\afxtls_.h",
            0x3c)) {
        CrtDebugBreak();
    }
    std::memcpy(bytes + 4, &element_size, sizeof(element_size));
    return list;
}

void* CTypeLibCache(void* cache) {
    if (cache != nullptr) {
        const std::int32_t minus_one = -1;
        const std::uint32_t zero = 0;
        auto* bytes = static_cast<unsigned char*>(cache);
        std::memcpy(bytes + 4, &minus_one, sizeof(minus_one));
        std::memcpy(bytes + 8, &zero, sizeof(zero));
        std::memcpy(bytes + 0x1c, &zero, sizeof(zero));
        std::memcpy(bytes + 0x20, &zero, sizeof(zero));
    }
    return cache;
}

void* GetMfcObjectVtable(const void* object) {
    return object != nullptr ? *static_cast<void* const*>(object) : nullptr;
}

int CompareMbcsCollationCaseSensitive(const char* lhs, const char* rhs) {
    return std::strcmp(lhs == nullptr ? "" : lhs, rhs == nullptr ? "" : rhs);
}

int CompareMbcsCollationCaseInsensitive(const char* lhs, const char* rhs) {
#ifdef _WIN32
    return _stricmp(lhs == nullptr ? "" : lhs, rhs == nullptr ? "" : rhs);
#else
    return CompareMbcsCollationCaseSensitive(lhs, rhs);
#endif
}

void DestroyMfcExceptionBase(void* exception);

void* DeleteMfcExceptionBase(void* exception, unsigned flags) {
    DestroyMfcExceptionBase(exception);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteNormalBlock(exception);
    }
    return exception;
}

void DestroyMfcExceptionBase(void* exception) {
    if (exception != nullptr) {
        DestroyExceptionBase(*static_cast<MfcSimpleExceptionCompat*>(exception));
    }
}
MfcRuntimeClassCompat* AfxClassInit(MfcRuntimeClassCompat* runtime_class);
void* GetMfcSimpleListNodeData(const void* owner, void* node) {
    if (node == nullptr &&
        AfxAssertFailedLine("E:\\8665\\vc98\\mfc\\mfc\\include\\afxtls_.h", 0x40)) {
        CrtDebugBreak();
    }
    int data_offset = 0;
    if (owner != nullptr) {
        const auto* bytes = static_cast<const unsigned char*>(owner);
        std::memcpy(&data_offset, bytes + 4, sizeof(data_offset));
    }
    const auto address = reinterpret_cast<std::uintptr_t>(node) +
        static_cast<std::uintptr_t>(data_offset);
    return reinterpret_cast<void*>(address);
}

void* GetMfcSimpleListNodeData(void* node) {
    return GetMfcSimpleListNodeData(nullptr, node);
}
void* ReturnSecondArgument(void*, void* second) { return second; }
void* AFX_CLASSINIT(void* initializer, MfcRuntimeClassCompat* runtime_class) {
    AfxClassInit(runtime_class);
    return initializer;
}

void AFX_CLASSINIT(MfcRuntimeClassCompat* runtime_class) {
    AfxClassInit(runtime_class);
}
void* ReturnInputPointer(void* value) { return value; }
int GetMfcSimpleArrayCount(const void* array) {
    if (array == nullptr) {
        return 0;
    }
    int count = 0;
    const auto* bytes = static_cast<const unsigned char*>(array);
    std::memcpy(&count, bytes + 4, sizeof(count));
    return count;
}

void* GetMfcSimpleArrayElementSlot(void* array, int index) {
    if (array == nullptr) {
        return nullptr;
    }
    const int count = GetMfcSimpleArrayCount(array);
    if (count <= index &&
        AfxAssertFailedLine("E:\\8665\\vc98\\mfc\\mfc\\include\\afxcoll.inl", 0x7b)) {
        CrtDebugBreak();
    }
    void* data_field = nullptr;
    const auto* bytes = static_cast<const unsigned char*>(array);
    std::memcpy(&data_field, bytes + 8, sizeof(data_field));
    auto* data = static_cast<unsigned char*>(data_field);
    return data != nullptr ? data + index * 4 : nullptr;
}

#ifdef _WIN32
namespace {

BSTR AllocateOleBstrFromAnsi(const char* text) {
    if (text == nullptr) {
        return nullptr;
    }

    const int wide_chars = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (wide_chars <= 0) {
        return SysAllocString(L"");
    }

    std::vector<wchar_t> wide(static_cast<std::size_t>(wide_chars));
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), wide_chars) <= 0) {
        return SysAllocString(L"");
    }
    return SysAllocString(wide.data());
}

SAFEARRAY* CreateOleByteSafeArray(const unsigned char* bytes,
    std::size_t byte_count) {
    SAFEARRAY* array = SafeArrayCreateVector(
        VT_UI1, 0, static_cast<ULONG>(byte_count));
    if (array == nullptr) {
        return nullptr;
    }

    if (byte_count == 0) {
        return array;
    }

    void* data = nullptr;
    if (FAILED(SafeArrayAccessData(array, &data))) {
        SafeArrayDestroy(array);
        return nullptr;
    }
    if (bytes != nullptr) {
        std::memcpy(data, bytes, byte_count);
    } else {
        std::memset(data, 0, byte_count);
    }
    SafeArrayUnaccessData(array);
    return array;
}

} // namespace

int DrawState(HDC dc, HBRUSH brush, DRAWSTATEPROC callback, LPARAM data,
    WPARAM data_length, int x, int y, int width, int height, UINT flags) {
    return DrawStateA(
        dc, brush, callback, data, data_length, x, y, width, height, flags);
}

VARIANTARG& Attach(VARIANTARG& target, VARIANTARG& source) {
    VariantClear(&target);
    target = source;
    VariantInit(&source);
    return target;
}

VARIANTARG& ConstructOleVariantFromCStringPointer(VARIANTARG& variant,
    const char* text) {
    VariantClear(&variant);
    V_VT(&variant) = VT_BSTR;
    V_BSTR(&variant) = AllocateOleBstrFromAnsi(text);
    return variant;
}

VARIANTARG& ConstructOleVariantFromCStringObject(VARIANTARG& variant,
    const MfcCStringCompat& text) {
    return ConstructOleVariantFromCStringPointer(
        variant, CStringGetStringPtr(text));
}

VARIANTARG& ConstructOleVariantFromI2OrBool(VARIANTARG& variant, short value,
    VARTYPE type) {
    VariantInit(&variant);
    V_VT(&variant) = type;
    if (type == VT_BOOL) {
        V_BOOL(&variant) = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
    } else {
        V_I2(&variant) = value;
    }
    return variant;
}

VARIANTARG& ConstructOleVariantFromI4ErrorOrBool(VARIANTARG& variant, LONG value,
    VARTYPE type) {
    VariantInit(&variant);
    V_VT(&variant) = type;
    if (type == VT_BOOL) {
        V_BOOL(&variant) = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (type == VT_ERROR) {
        V_ERROR(&variant) = value;
    } else {
        V_I4(&variant) = value;
    }
    return variant;
}

OleCurrencyCompat FinishOleCurrencyFromVariant(const VARIANTARG& variant) {
    return ConstructOleCurrencyFromVariant(variant);
}

// Ghidra original symbol: FID_conflict:COleCurrency.
OleCurrencyCompat FID_conflict_COleCurrency(CY value) {
    OleCurrencyCompat result{};
    result.value = value;
    result.status = OleDateStatus::Valid;
    return result;
}

OleCurrencyCompat COleCurrency(LONG units, LONG fractional_10000) {
    return SetOleCurrencyParts(units, fractional_10000);
}

// Ghidra original symbol at 005f7788: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_add_assign(
    OleCurrencyCompat& value, const OleCurrencyCompat& rhs) {
    value = AddOleCurrency(value, rhs);
    return value;
}

// Ghidra original symbol at 005f77b3: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_subtract_assign(
    OleCurrencyCompat& value, const OleCurrencyCompat& rhs) {
    value = SubtractOleCurrency(value, rhs);
    return value;
}

// Ghidra original symbol at 005f77de: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_multiply_assign(
    OleCurrencyCompat& value, LONG factor) {
    value = MultiplyOleCurrencyByLong(value, factor);
    return value;
}

// Ghidra original symbol at 005f7809: FID_conflict:operator/=.
OleCurrencyCompat& FID_conflict_operator_divide_assign(
    OleCurrencyCompat& value, LONG divisor) {
    value = DivideOleCurrencyByLong(value, divisor);
    return value;
}

bool OleCurrencyRawEquals(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return lhs.status == rhs.status && lhs.value.int64 == rhs.value.int64;
}

bool OleCurrencyRawNotEquals(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return !OleCurrencyRawEquals(lhs, rhs);
}

CY operator_union_tagCY(const OleCurrencyCompat& value) {
    return value.value;
}

bool CompareOleCurrencyLess(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) < 0;
}

bool CompareOleCurrencyGreater(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) > 0;
}

bool CompareOleCurrencyLessEqual(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) <= 0;
}

bool CompareOleCurrencyGreaterEqual(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    return CompareOleCurrency(lhs, rhs) >= 0;
}

std::tm AdjustDecodedDateTimeForTm(const std::tm& value) { return value; }
double NormalizeNegativeOleDateIntegral(double value) { return value; }
double NormalizeNegativeOleDateFraction(double value) { return value; }

OleDateTimeCompat ConstructOleDateTimeFromTimeT(std::time_t value);

OleDateTimeCompat GetCurrentOleDateTime() {
    return ConstructOleDateTimeFromTimeT(CrtTime(nullptr));
}

bool GetOleDateTimeAsSystemTime(const OleDateTimeCompat& value,
    SYSTEMTIME& out) {
    return DecodeOleDateTime(value, out);
}

int GetOleDateTimeSecond(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wSecond : 0;
}

int GetOleDateTimeMinute(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wMinute : 0;
}

int GetOleDateTimeHour(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wHour : 0;
}

int GetOleDateTimeDay(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wDay : 0;
}

int GetOleDateTimeMonth(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wMonth : 0;
}

int GetOleDateTimeYear(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wYear : 0;
}

int GetOleDateTimeDayOfWeek(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    return DecodeOleDateTime(value, out) ? out.wDayOfWeek : 0;
}

int GetOleDateTimeDayOfYear(const OleDateTimeCompat& value) {
    SYSTEMTIME out{};
    if (!DecodeOleDateTime(value, out)) {
        return 0;
    }
    static constexpr int days_before_month[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const bool leap = (out.wYear % 4 == 0 && out.wYear % 100 != 0) ||
        out.wYear % 400 == 0;
    return days_before_month[out.wMonth] + out.wDay +
        (leap && out.wMonth > 2 ? 1 : 0);
}

OleDateTimeCompat FinishOleDateTimeFromVariant(const VARIANTARG& variant) {
    return ConstructOleDateTimeFromVariant(variant);
}

OleDateTimeCompat ConstructOleDateTimeFromTimeT(std::time_t value) {
    const std::tm* decoded = std::localtime(&value);
    if (decoded == nullptr) {
        return OleDateTimeCompat{0.0, OleDateStatus::Invalid};
    }
    return ConstructOleDateTimeFromFields(
        static_cast<WORD>(decoded->tm_year + 1900),
        static_cast<WORD>(decoded->tm_mon + 1),
        static_cast<WORD>(decoded->tm_mday),
        static_cast<WORD>(decoded->tm_hour),
        static_cast<WORD>(decoded->tm_min),
        static_cast<WORD>(decoded->tm_sec));
}

bool CompareOleDateTimeLess(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) < 0;
}

bool CompareOleDateTimeGreater(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) > 0;
}

bool CompareOleDateTimeLessEqual(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) <= 0;
}

bool CompareOleDateTimeGreaterEqual(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    return CompareOleDateTime(lhs, rhs) >= 0;
}

OleDateTimeSpanCompat SubtractOleDateTimeSpan(
    const OleDateTimeSpanCompat& lhs, const OleDateTimeSpanCompat& rhs) {
    return SubtractOleDateTimeSpans(lhs, rhs);
}

OleDateTimeCompat SetOleDateTime(WORD year, WORD month, WORD day, WORD hour,
    WORD minute, WORD second) {
    return ConstructOleDateTimeFromFields(year, month, day, hour, minute, second);
}

int SetDate(OleDateTimeCompat& value, int year, int month, int day) {
    value = ConstructOleDateTimeFromFields(static_cast<WORD>(year),
        static_cast<WORD>(month), static_cast<WORD>(day), 0, 0, 0);
    return static_cast<int>(value.status);
}

int SetTime(OleDateTimeCompat& value, int hour, int minute, int second) {
    value = ConstructOleDateTimeFromFields(1899, 12, 30,
        static_cast<WORD>(hour), static_cast<WORD>(minute),
        static_cast<WORD>(second));
    return static_cast<int>(value.status);
}

bool CheckOleDateTimeRange(const OleDateTimeCompat& value) {
    return value.status == OleDateStatus::Valid;
}

void SerializeOleDateTimeToArchive(const OleDateTimeCompat& value, void* archive) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    ArchiveWriteDWordInline(*typed_archive, static_cast<DWORD>(value.status));
    ArchiveWriteDoubleInline(*typed_archive, value.value);
}

OleDateTimeSpanCompat& AssignOleDateTimeSpan(OleDateTimeSpanCompat& target,
    const OleDateTimeSpanCompat& source) {
    target = source;
    return target;
}
bool CheckOleDateTimeSpanRange(const OleDateTimeSpanCompat& value) {
    return value.status == OleDateStatus::Valid;
}
void SerializeOleDateTimeSpanToArchive(const OleDateTimeSpanCompat& value,
    void* archive) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    ArchiveWriteDWordInline(*typed_archive, static_cast<DWORD>(value.status));
    ArchiveWriteDoubleInline(*typed_archive, value.days);
}

VARIANTARG& AssignOleSafeArrayFromArray(VARIANTARG& target,
    const VARIANTARG& source) {
    VariantCopy(&target, const_cast<VARIANTARG*>(&source));
    return target;
}
SAFEARRAY* OleVariantArrayPointer(const VARIANTARG& value) {
    VARIANTARG* mutable_value = const_cast<VARIANTARG*>(&value);
    return (V_VT(mutable_value) & VT_ARRAY) != 0
        ? V_ARRAY(mutable_value) : nullptr;
}

bool CompareOleSafeArrayVariantRef(const VARIANTARG& lhs,
    const VARIANTARG& rhs) {
    return CompareSafeArrays(OleVariantArrayPointer(lhs),
        OleVariantArrayPointer(rhs));
}
bool CompareOleSafeArrayVariantPtr(const VARIANTARG* lhs,
    const VARIANTARG* rhs) {
    return lhs == rhs || (lhs != nullptr && rhs != nullptr &&
        CompareOleSafeArrayVariantRef(*lhs, *rhs));
}
bool CompareOleSafeArrayObjectRef(const VARIANTARG& lhs,
    const VARIANTARG& rhs) {
    return CompareOleSafeArrayVariantRef(lhs, rhs);
}
bool CompareOleSafeArrayObjectPtr(const VARIANTARG* lhs,
    const VARIANTARG* rhs) {
    return CompareOleSafeArrayVariantPtr(lhs, rhs);
}

// Ghidra original symbol: COleSafeArray::operator==.
bool COleSafeArray_operator_equal(const VARIANTARG& lhs,
    const VARIANTARG& rhs) {
    return CompareOleSafeArrayVariantRef(lhs, rhs);
}

// Ghidra original symbol: FID_conflict:GetDim.
UINT FID_conflict_GetDim(SAFEARRAY* array) {
    return SafeArrayGetDim(array);
}

// Ghidra original symbol at 005f81d8: FID_conflict:GetDim.
UINT FID_conflict_GetElemSize(SAFEARRAY* array) {
    return SafeArrayGetElemsize(array);
}

UINT FID_conflict_GetDim(const VARIANTARG& variant) {
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    return (V_VT(mutable_variant) & VT_ARRAY) != 0 &&
            V_ARRAY(mutable_variant) != nullptr
        ? SafeArrayGetDim(V_ARRAY(mutable_variant))
        : 0;
}

UINT FID_conflict_GetElemSize(const VARIANTARG& variant) {
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    return (V_VT(mutable_variant) & VT_ARRAY) != 0 &&
            V_ARRAY(mutable_variant) != nullptr
        ? SafeArrayGetElemsize(V_ARRAY(mutable_variant))
        : 0;
}

// Ghidra original symbol at 0051d30c: FID_conflict:GetUBound.
HRESULT FID_conflict_GetLBound(SAFEARRAY* array, UINT dimension,
    LONG* lower_bound) {
    return array == nullptr ? E_POINTER
        : SafeArrayGetLBound(array, dimension, lower_bound);
}

// Ghidra original symbol: FID_conflict:GetUBound.
LONG FID_conflict_GetUBound(SAFEARRAY* array, UINT dimension) {
    LONG upper_bound = 0;
    return array != nullptr &&
            SUCCEEDED(SafeArrayGetUBound(array, dimension, &upper_bound))
        ? upper_bound
        : 0;
}

// Ghidra original symbol at 0051d334: FID_conflict:GetUBound.
HRESULT FID_conflict_GetUBound(SAFEARRAY* array, UINT dimension,
    LONG* upper_bound) {
    return array == nullptr ? E_POINTER
        : SafeArrayGetUBound(array, dimension, upper_bound);
}

LONG FID_conflict_GetUBound(const VARIANTARG& variant, UINT dimension) {
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    return (V_VT(mutable_variant) & VT_ARRAY) != 0
        ? FID_conflict_GetUBound(V_ARRAY(mutable_variant), dimension)
        : 0;
}

// Ghidra original symbol at 0051d35c: FID_conflict:GetUBound.
HRESULT FID_conflict_GetElement(SAFEARRAY* array, LONG* indices, void* out) {
    return array == nullptr ? E_POINTER
        : SafeArrayGetElement(array, indices, out);
}

// Ghidra original symbol at 0051d384: FID_conflict:GetUBound.
HRESULT FID_conflict_PtrOfIndex(SAFEARRAY* array, LONG* indices, void** out) {
    return array == nullptr ? E_POINTER
        : SafeArrayPtrOfIndex(array, indices, out);
}

// Ghidra original symbol at 0051d3ac: FID_conflict:GetUBound.
HRESULT FID_conflict_PutElement(SAFEARRAY* array, LONG* indices, void* value) {
    return array == nullptr ? E_POINTER
        : SafeArrayPutElement(array, indices, value);
}

SAFEARRAY* CreateMultiDimOleSafeArrayFromCounts(VARTYPE type,
    const std::vector<LONG>& counts) {
    if (counts.empty()) {
        return nullptr;
    }
    std::vector<SAFEARRAYBOUND> bounds(counts.size());
    for (std::size_t index = 0; index < counts.size(); ++index) {
        bounds[index].lLbound = 0;
        bounds[index].cElements = static_cast<ULONG>(std::max<LONG>(counts[index], 0));
    }
    return SafeArrayCreate(type, static_cast<UINT>(bounds.size()), bounds.data());
}
SAFEARRAY* FinishCreateMultiDimOleSafeArray(SAFEARRAY* array) { return array; }
HRESULT AllocDescriptor(UINT dimensions, SAFEARRAY** array) {
    return SafeArrayAllocDescriptor(dimensions, array);
}

VARIANTARG& AssignOleVariantFromByteArray(VARIANTARG& variant,
    const MfcByteArrayCompat& array) {
    SAFEARRAY* bytes = CreateOleByteSafeArray(
        array.values.empty() ? nullptr : array.values.data(),
        array.values.size());
    VariantClear(&variant);
    V_VT(&variant) = VT_ARRAY | VT_UI1;
    V_ARRAY(&variant) = bytes;
    return variant;
}

void SerializeOleVariantToArchive(const VARIANTARG& variant, void* archive) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    VARIANTARG* mutable_variant = const_cast<VARIANTARG*>(&variant);
    const VARTYPE type = V_VT(mutable_variant);
    ArchiveWriteWordInline(*typed_archive, type);
    if ((type & (VT_ARRAY | VT_BYREF)) != 0) {
        return;
    }
    switch (type) {
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I2:
        ArchiveWriteWordInline(*typed_archive,
            static_cast<unsigned short>(V_I2(mutable_variant)));
        break;
    case VT_I4:
        ArchiveWriteDWordInline(*typed_archive,
            static_cast<unsigned>(V_I4(mutable_variant)));
        break;
    case VT_R4:
        ArchiveWriteFloatInline(*typed_archive, V_R4(mutable_variant));
        break;
    case VT_R8:
    case VT_DATE:
        ArchiveWriteDoubleInline(*typed_archive, V_R8(mutable_variant));
        break;
    case VT_CY: {
        const auto raw = static_cast<unsigned long long>(
            V_CY(mutable_variant).int64);
        ArchiveWriteLongInline(*typed_archive,
            static_cast<long>(raw & 0xffffffffULL));
        ArchiveWriteDWordInline(*typed_archive,
            static_cast<unsigned>(raw >> 32));
        break;
    }
    case VT_BSTR: {
        BSTR text = V_BSTR(mutable_variant);
        const unsigned bytes = text != nullptr ? SysStringByteLen(text) : 0;
        ArchiveWriteLongInline(*typed_archive, static_cast<long>(bytes));
        if (bytes != 0) {
            ArchiveWrite(*typed_archive, text, bytes);
        }
        break;
    }
    case VT_ERROR:
        ArchiveWriteDWordInline(*typed_archive,
            static_cast<unsigned>(V_ERROR(mutable_variant)));
        break;
    case VT_BOOL:
        ArchiveWriteWordInline(*typed_archive,
            static_cast<unsigned short>(V_BOOL(mutable_variant)));
        break;
    case VT_UI1:
        ArchiveWriteByteInline(*typed_archive, V_UI1(mutable_variant));
        break;
    default:
        break;
    }
}

void FinishSerializeOleVariantStream(void*) {}

void DeserializeOleVariantFromArchive(VARIANTARG& variant, void* archive) {
    VariantClear(&variant);
    VariantInit(&variant);
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    unsigned short type = VT_EMPTY;
    ArchiveReadWordInline(*typed_archive, type);
    V_VT(&variant) = type;
    if ((type & (VT_ARRAY | VT_BYREF)) != 0) {
        return;
    }
    switch (type) {
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I2: {
        unsigned short value = 0;
        ArchiveReadWordInline(*typed_archive, value);
        V_I2(&variant) = static_cast<SHORT>(value);
        break;
    }
    case VT_I4: {
        unsigned value = 0;
        ArchiveReadDWordInline(*typed_archive, value);
        V_I4(&variant) = static_cast<LONG>(value);
        break;
    }
    case VT_R4:
        ArchiveReadFloatInline(*typed_archive, V_R4(&variant));
        break;
    case VT_R8:
    case VT_DATE:
        ArchiveReadDoubleInline(*typed_archive, V_R8(&variant));
        break;
    case VT_CY: {
        long low = 0;
        unsigned high = 0;
        ArchiveReadLongInline(*typed_archive, low);
        ArchiveReadDWordInline(*typed_archive, high);
        const unsigned long long raw =
            (static_cast<unsigned long long>(high) << 32) |
            static_cast<unsigned long>(low);
        V_CY(&variant).int64 = static_cast<long long>(raw);
        break;
    }
    case VT_BSTR: {
        long byte_count = 0;
        ArchiveReadLongInline(*typed_archive, byte_count);
        if (byte_count > 0) {
            BSTR text = SysAllocStringByteLen(nullptr,
                static_cast<unsigned>(byte_count));
            if (text != nullptr) {
                ArchiveRead(*typed_archive, text,
                    static_cast<unsigned>(byte_count));
                V_BSTR(&variant) = text;
            } else {
                std::array<char, 256> discard{};
                long remaining = byte_count;
                while (remaining > 0) {
                    const unsigned chunk = static_cast<unsigned>(
                        std::min<long>(remaining,
                            static_cast<long>(discard.size())));
                    ArchiveRead(*typed_archive, discard.data(), chunk);
                    remaining -= static_cast<long>(chunk);
                }
            }
        }
        break;
    }
    case VT_ERROR: {
        unsigned value = 0;
        ArchiveReadDWordInline(*typed_archive, value);
        V_ERROR(&variant) = static_cast<SCODE>(value);
        break;
    }
    case VT_BOOL: {
        unsigned short value = 0;
        ArchiveReadWordInline(*typed_archive, value);
        V_BOOL(&variant) = static_cast<VARIANT_BOOL>(value);
        break;
    }
    case VT_UI1:
        ArchiveReadByteInline(*typed_archive, V_UI1(&variant));
        break;
    default:
        break;
    }
}
void FinishDeserializeOleVariantStream(void*) {}

VARIANTARG* ConstructOleVariantArray(VARIANTARG* values, int count) {
    if (values == nullptr || count <= 0) {
        return values;
    }
    for (int index = 0; index < count; ++index) {
        VariantInit(&values[index]);
    }
    return values;
}

void DestructOleVariantArray(VARIANTARG* values, int count) {
    if (values == nullptr || count <= 0) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        VariantClear(&values[index]);
    }
}

VARIANTARG* CopyOleVariantArray(VARIANTARG* destination,
    const VARIANTARG* source, int count) {
    if (destination == nullptr || source == nullptr || count <= 0) {
        return destination;
    }
    for (int index = 0; index < count; ++index) {
        VariantInit(&destination[index]);
        VariantCopy(&destination[index],
            const_cast<VARIANTARG*>(&source[index]));
    }
    return destination;
}

void SerializeOleVariantArray(VARIANTARG* values, int count, void* archive) {
    if (values == nullptr || count <= 0) {
        return;
    }
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        if (ArchiveIsLoading(*typed_archive)) {
            DeserializeOleVariantFromArchive(values[index], archive);
        } else {
            SerializeOleVariantToArchive(values[index], archive);
        }
    }
}

void DeleteOleVariantObject(VARIANTARG& variant) { VariantClear(&variant); }

void AfxCheckError(HRESULT result);

// Ghidra original symbol: COleVariant::operator=.
VARIANTARG& COleVariant_operator_assign(VARIANTARG& target,
    const VARIANTARG& source) {
    const HRESULT result = VariantCopy(&target, const_cast<VARIANTARG*>(&source));
    AfxCheckError(result);
    return target;
}

void* COleDispatchDriver(void* driver) { return driver; }

void InvokeHelper(MfcCWndCompat& window, LONG dispatch_id, WORD flags,
    unsigned short return_type, void* return_value,
    const unsigned char* param_info = nullptr) {
    CWndInvokeHelper(window, dispatch_id, flags, return_type, return_value,
        param_info);
}

void GetProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, void* value) {
    CWndGetProperty(window, dispatch_id, value_type, value);
}

int IsResultExpected(MfcCommandTargetCompat& target) {
    const int result = target.result_expected ? TRUE : FALSE;
    target.result_expected = true;
    return result;
}

u32 HashPointerShift4(const void* value) {
    return static_cast<u32>(
        reinterpret_cast<std::uintptr_t>(value) >> 4);
}

void* ReturnSecondArgumentThunk(void*, void* second) { return second; }
void NoopRuntimeThunk() {}

void* InstallMfcThreadStateSlotC4(void*, void* replacement) {
    return replacement;
}

void* RestoreMfcThreadStateSlotC4(void* value) { return value; }

std::size_t SpanMbcsStringIncludingThunk(const char* text,
    const char* characters) {
    return SpanMbcsStringIncluding(text, characters);
}

std::size_t SpanMbcsStringExcludingThunk(const char* text,
    const char* characters) {
    return SpanMbcsStringExcluding(text, characters);
}

int CompareMbcsStringNThunk(const char* lhs, const char* rhs,
    std::size_t count) {
    return CompareMbcsStringN(lhs, rhs, count);
}

int GetMbcsCharacterLengthThunk(const char* text) {
    return GetMbcsCharacterLength(text);
}

void* InstallMfcThreadStateSlotC0(void*, void* replacement) {
    return replacement;
}

void* RestoreMfcThreadStateSlotC0(void* value) { return value; }

bool CompareGuidBytesNotEqual(const void* lhs, const void* rhs) {
    return CompareGuidBytes(lhs, rhs) == 0;
}

int GetFileTitleAThunk(LPCSTR path, LPSTR title, WORD title_chars) {
    return GetFileTitleA(path, title, title_chars);
}

const char* PreviousMbcsStringPointerThunk(const char* start,
    const char* current) {
    return PreviousMbcsStringPointer(start, current);
}

void* LockTypeLibCacheEntry(void* entry) { return entry; }
MfcMapPtrToPtrCompat& CTypeLibCacheMap(MfcMapPtrToPtrCompat& map) {
    return ConstructMapPtrToPtr(map, 10);
}

void* CTypeLibCacheMap(void* map) {
    return map == nullptr ? nullptr
        : &CTypeLibCacheMap(*static_cast<MfcMapPtrToPtrCompat*>(map));
}

void InitString(MfcSimpleExceptionCompat& exception) {
    exception.string_initialized = true;
    exception.has_message = exception.message[0] != '\0';
    if (exception.has_message || exception.help_context == 0) {
        return;
    }
    char buffer[128]{};
    if (AfxLoadStringCompat(exception.help_context, buffer,
        static_cast<int>(sizeof(buffer))) != 0) {
        std::strncpy(exception.message.data(), buffer,
            exception.message.size() - 1);
        exception.has_message = exception.message[0] != '\0';
    }
}

MfcSimpleExceptionCompat& ConstructSimpleExceptionWithFlags(
    MfcSimpleExceptionCompat& exception, int auto_delete,
    unsigned help_context) {
    return ConstructSimpleException(
        exception, auto_delete != 0, help_context);
}

// Ghidra original symbol: FID_conflict:CNotSupportedException.
MfcSimpleExceptionCompat& FID_conflict_CNotSupportedException(
    MfcSimpleExceptionCompat& exception, int auto_delete,
    unsigned help_context) {
    ConstructNotSupportedException(exception);
    exception.auto_delete = auto_delete != 0;
    exception.help_context = help_context;
    return exception;
}

std::tm* GetLocalTm(const MfcTimeCompat& value, std::tm* out) {
    std::tm* source = std::localtime(&value.value);
    if (source == nullptr) {
        return nullptr;
    }
    if (out == nullptr) {
        return source;
    }
    *out = *source;
    return out;
}

// Ghidra original symbol: FID_conflict:GetLocalTm.
std::tm* FID_conflict_GetLocalTm(const MfcTimeCompat& value, std::tm* out) {
    return GetLocalTm(value, out);
}

COLORREF* GetSavedCustomColors() {
    return GetSavedCustomColorsCompat();
}

int FindCStringSubstringFrom(const MfcCStringCompat& text, const char* needle,
    int start_index) {
    return FindCStringSubstring(text, needle, start_index);
}

// Ghidra original symbol: HashKey<char_const*>.
unsigned HashKeyCharConst(const char* key) {
    unsigned hash = 0;
    if (key == nullptr) {
        return hash;
    }
    for (; *key != '\0'; ++key) {
        hash = hash * 0x21u + static_cast<unsigned>(static_cast<int>(*key));
    }
    return hash;
}

int BeginDrag(MfcDragListBoxCompat& box, POINT point) {
    return BeginDragListBox(box, point);
}

int InsertColumn(MfcListCtrlCompat& control, int column, const char* heading,
    int format, int width, int subitem) {
    return ListCtrlInsertColumn(control, column, heading, format, width, subitem);
}

void OnNcDestroy(MfcListCtrlCompat& control) {
    ListCtrlOnNcDestroy(control);
}

void OnDestroy(MfcTreeCtrlCompat& control) {
    TreeCtrlOnDestroy(control);
}

// Ghidra original symbol: FID_conflict:~CDragListBox.
void FID_conflict_DestructCDragListBox(MfcDragListBoxCompat& box) {
    if (box.window != nullptr) {
        DestroyWindow(box.window);
    }
    box.window = nullptr;
    box.style = 0;
    box.last_insert = -1;
}

void SetRange(MfcSliderCtrlCompat& control, int lower, int upper, int redraw) {
    SliderSetRange(control, lower, upper, redraw != 0);
}

// Ghidra original symbol: FID_conflict:CImageList.
MfcImageListCompat& FID_conflict_CImageList(MfcImageListCompat& image_list) {
    image_list.handle = nullptr;
    image_list.owns_handle = true;
    return image_list;
}

MfcMenuCompat& FID_conflict_CImageList(MfcMenuCompat& menu) {
    return ConstructMenuCompat(menu);
}

// Ghidra original symbol: FID_conflict:~CMenu.
void FID_conflict_DestructCMenu(MfcMenuCompat& menu) {
    DestroyMenuCompat(menu);
}

void FID_conflict_DestructImageList(MfcImageListCompat& image_list) {
    if (image_list.handle != nullptr && image_list.owns_handle) {
        ImageList_Destroy(image_list.handle);
    }
    image_list.handle = nullptr;
    image_list.owns_handle = true;
}

void FID_conflict_DestructGdiObject(MfcGdiObjectCompat& object) {
    DestroyGdiObjectCompat(object);
}

// Ghidra original symbol: operator_void*.
void* operator_void(const MfcGdiObjectCompat* object) {
    return object == nullptr ? nullptr : object->object;
}

// Ghidra original symbol: operator_void*.
void* operator_void(const MfcWinThreadCompat* thread) {
    return thread == nullptr ? nullptr : thread->thread;
}

// Ghidra original symbol: FID_conflict:GetSafeHdc.
void* FID_conflict_GetSafeHdc(const MfcGdiObjectCompat* object) {
    return object == nullptr ? nullptr : object->object;
}

HDC FID_conflict_GetSafeHdc(const MfcCDCCompat* dc) {
    return CDCGetSafeHdc(dc);
}

int CreateStockObject(MfcGdiObjectCompat& object, int stock_object) {
    object.object = GetStockObject(stock_object);
    object.temporary = false;
    return object.object != nullptr ? TRUE : FALSE;
}

bool operator==(const MfcGdiObjectCompat& left,
    const MfcGdiObjectCompat& right) {
    return left.object == right.object;
}

bool operator!=(const MfcGdiObjectCompat& left,
    const MfcGdiObjectCompat& right) {
    return !(left == right);
}

MfcGdiObjectCompat* FromHandle(HFONT font) {
    return FontFromHandle(font);
}

// Ghidra original symbol: FID_conflict:LookupTemporary.
MfcCWndCompat* FID_conflict_LookupTemporary(HWND handle) {
    return CWndFromHandle(handle);
}

MfcMenuCompat* FID_conflict_LookupTemporary(HMENU handle) {
    return CMenuFromHandle(handle);
}

MfcImageListCompat* FID_conflict_LookupTemporary(HIMAGELIST handle) {
    return LookupTemporaryImageList(handle);
}

MfcGdiObjectCompat* FID_conflict_LookupTemporary(HGDIOBJ handle) {
    return GdiObjectFromHandleCompat(handle);
}

// Ghidra original symbol: FID_conflict:DeleteObject.
BOOL FID_conflict_DeleteObject(MfcImageListCompat& image_list) {
    HIMAGELIST handle = image_list.handle;
    image_list.handle = nullptr;
    image_list.owns_handle = true;
    return handle == nullptr ? FALSE : ImageList_Destroy(handle);
}

BOOL FID_conflict_DeleteObject(MfcMenuCompat& menu) {
    if (menu.menu == nullptr) {
        return FALSE;
    }
    HMENU handle = menu.menu;
    menu.menu = nullptr;
    menu.temporary = false;
    return DestroyMenu(handle);
}

BOOL FID_conflict_DeleteObject(MfcGdiObjectCompat& object) {
    return GdiObjectDeleteObject(object);
}

int DestroyToolTipCtrl(MfcToolTipCtrlCompat& control) {
    if (control.window != nullptr) {
        DestroyWindow(control.window);
    }
    control.window = nullptr;
    control.style = 0;
    return TRUE;
}

long OnDisableModal(MfcToolTipCtrlCompat& control, UINT, long) {
    if (control.window != nullptr) {
        SendMessageA(control.window, TTM_ACTIVATE, FALSE, 0);
    }
    return 0;
}

void OnEnable(MfcToolTipCtrlCompat& control, int enabled) {
    if (control.window != nullptr) {
        SendMessageA(control.window, TTM_ACTIVATE, enabled ? TRUE : FALSE, 0);
    }
}

void ToolTipFilterRelayMessageThunk(MfcToolTipCtrlCompat& control,
    const MSG& message) {
    ToolTipFilterRelayMessage(control, message);
}

MfcArchiveStreamCompat& CArchiveStream(MfcArchiveStreamCompat& stream,
    void* archive) {
    stream.archive = archive;
    stream.buffer.clear();
    stream.position = 0;
    return stream;
}

MfcPtrListNodeCompat* AddHead(MfcPtrListCompat& list, void* value) {
    return PtrListAddHead(list, value);
}

MfcPtrListNodeCompat* AddTail(MfcPtrListCompat& list, void* value) {
    return PtrListAddTail(list, value);
}

// Ghidra original symbol: FID_conflict:CDWordArray.
MfcByteArrayCompat& FID_conflict_CDWordArray(MfcByteArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcWordArrayCompat& FID_conflict_CDWordArray(MfcWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcDWordArrayCompat& FID_conflict_CDWordArray(MfcDWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcObArrayCompat& FID_conflict_CDWordArray(MfcObArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcCStringArrayCompat& FID_conflict_CDWordArray(
    MfcCStringArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

// Ghidra original symbol: FID_conflict:CArray<int,int_const&>.
MfcUIntArrayCompat& FID_conflict_CArrayInt(MfcUIntArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

MfcPtrArrayCompat& FID_conflict_CArrayInt(MfcPtrArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
    return array;
}

void DeleteValues(MfcPtrArrayCompat& array) {
    array.values.clear();
}

void DeleteValues(MfcObArrayCompat& array) {
    array.values.clear();
}

void DeleteValues(std::vector<void*>& values) {
    values.clear();
}

// Ghidra original symbol: FID_conflict:~CPtrArray.
void FID_conflict_DestructCPtrArray(MfcByteArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcDWordArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcUIntArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcPtrArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcObArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

void FID_conflict_DestructCPtrArray(MfcCStringArrayCompat& array) {
    array.values.clear();
    array.grow_by = 0;
}

int Difference(CrtMemState& diff, const CrtMemState& old_state,
    const CrtMemState& new_state) {
    return CrtMemDifference(diff, old_state, new_state, false) ? 1 : 0;
}

void Checkpoint(CrtMemState& state) {
    CrtMemCheckpoint(state);
}

int GetErrorMessage(char* destination, unsigned destination_chars,
    unsigned* help_context) {
    if (help_context != nullptr) {
        *help_context = 0;
    }
    if (destination != nullptr && destination_chars != 0) {
        destination[0] = '\0';
    }
    return FALSE;
}

int GetErrorMessage(MfcSimpleExceptionCompat& exception, char* destination,
    unsigned destination_chars, unsigned* help_context) {
    return GetSimpleExceptionErrorMessage(exception, destination,
        static_cast<int>(destination_chars), help_context) ? TRUE : FALSE;
}

AfxExceptionLinkCompat& AFX_EXCEPTION_LINK(AfxExceptionLinkCompat& link) {
    AfxExceptionContextCompat& context = AfxExceptionContextCompatState();
    link.previous = context.current;
    link.exception = nullptr;
    context.current = &link;
    return link;
}

int IsSerializable(const MfcObjectCompat& object) {
    return object.runtime_class != nullptr &&
        object.runtime_class->schema != 0xffff ? TRUE : FALSE;
}

MfcCWndCompat* GetMainWnd(MfcWinThreadCompat& thread) {
    if (thread.main_window != nullptr) {
        return CWndFromHandle(thread.main_window);
    }
    if (thread.active_window != nullptr) {
        return CWndFromHandle(thread.active_window);
    }
    return CWndFromHandle(GetActiveWindow());
}

int IsEnterKey(const MSG* message) {
    return message != nullptr && message->message == WM_KEYDOWN &&
        message->wParam == VK_RETURN ? TRUE : FALSE;
}

int ModifyStyle(HWND window, DWORD remove_bits, DWORD add_bits, UINT flags) {
    return ModifyWindowLongStyle(window, GWL_STYLE, remove_bits, add_bits,
        flags) ? TRUE : FALSE;
}

int ModifyStyleEx(HWND window, DWORD remove_bits, DWORD add_bits, UINT flags) {
    return ModifyWindowLongStyle(window, GWL_EXSTYLE, remove_bits, add_bits,
        flags) ? TRUE : FALSE;
}

const MSG* GetCurrentMessage() {
    static MSG fallback{};
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    MSG& message = thread != nullptr ? thread->current_message : fallback;
    message.time = GetMessageTime();
    const DWORD pos = GetMessagePos();
    message.pt.x = static_cast<short>(LOWORD(pos));
    message.pt.y = static_cast<short>(HIWORD(pos));
    return &message;
}

// Ghidra original symbol: FID_conflict:DefWindowProcA.
LRESULT FID_conflict_DefWindowProcA(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    HWND hwnd = window.window;
    if (hwnd == nullptr) {
        return 0;
    }
    if (window.original_wnd_proc != nullptr) {
        return CallWindowProcA(window.original_wnd_proc, hwnd, message, wparam,
            lparam);
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT Default(MfcCWndCompat& window) {
    const MSG* message = GetCurrentMessage();
    HWND hwnd = message->hwnd != nullptr ? message->hwnd : window.window;
    if (hwnd == nullptr) {
        return 0;
    }
    if (window.original_wnd_proc != nullptr) {
        return CallWindowProcA(window.original_wnd_proc, hwnd,
            message->message, message->wParam, message->lParam);
    }
    return DefWindowProcA(hwnd, message->message, message->wParam,
        message->lParam);
}

int OnCompareItem(MfcCWndCompat& window, int, COMPAREITEMSTRUCT* compare) {
    LRESULT result = 0;
    if (compare != nullptr &&
        CWndSendChildNotifyLastMsgByHandle(compare->hwndItem, &result)) {
        return static_cast<int>(result);
    }
    return static_cast<int>(Default(window));
}

void OnDeleteItem(MfcCWndCompat& window, int, DELETEITEMSTRUCT* item) {
    if (item == nullptr ||
        !CWndSendChildNotifyLastMsgByHandle(item->hwndItem, nullptr)) {
        Default(window);
    }
}

// Ghidra original symbol: FID_conflict:OnCharToItem.
long FID_conflict_OnCharToItem(MfcCWndCompat& window, UINT,
    MfcListBoxCompat* list_box, UINT) {
    LRESULT result = 0;
    if (list_box != nullptr &&
        CWndSendChildNotifyLastMsgByHandle(list_box->window, &result)) {
        return result;
    }
    return Default(window);
}

long FID_conflict_OnCharToItem(MfcCWndCompat& window, UINT,
    MfcCWndCompat* child, UINT) {
    LRESULT result = 0;
    if (child != nullptr &&
        CWndSendChildNotifyLastMsgByHandle(child->window, &result)) {
        return result;
    }
    return Default(window);
}

long WindowProc(MfcCWndCompat& window, UINT message, WPARAM wparam,
    LPARAM lparam) {
    LRESULT result = 0;
    if (CWndOnWndMsg(window, message, wparam, lparam, &result)) {
        return result;
    }
    return FID_conflict_DefWindowProcA(window, message, wparam, lparam);
}

// Ghidra original symbol: FID_conflict:CTestCmdUI.
MfcCmdUICompat& FID_conflict_CTestCmdUI(MfcCmdUICompat& cmd_ui) {
    ConstructCmdUI(cmd_ui);
    cmd_ui.kind = MfcCmdUIKind::Test;
    cmd_ui.flags = 1;
    return cmd_ui;
}

void Enable(MfcCmdUICompat& cmd_ui, int enabled) {
    cmd_ui.flags = enabled ? 1U : 0U;
    cmd_ui.changed = true;
}

void SetPermanent(MfcHandleMapCompat& map, void* handle, void* object) {
    if (handle == nullptr) {
        return;
    }
    map.temporary.erase(handle);
    map.permanent[handle] = object;
}

IUnknown* GetControlUnknown(MfcCWndCompat& window) {
    auto* site = static_cast<MfcOleControlSiteCompat*>(window.control_site);
    return site == nullptr ? nullptr : site->control_unknown;
}

HRESULT InternalQueryInterface(IUnknown* unknown, REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    return unknown == nullptr ? E_NOINTERFACE
        : unknown->QueryInterface(iid, object);
}

HRESULT InternalQueryInterface(MfcCommandTargetCompat&, IUnknown* outer_unknown,
    REFIID iid, void** object) {
    return InternalQueryInterface(outer_unknown, iid, object);
}

IUnknown* GetControllingUnknown(IUnknown* unknown) {
    return unknown;
}

MfcCmdUICompat& CCmdUI(MfcCmdUICompat& cmd_ui) {
    return ConstructCmdUI(cmd_ui);
}

void ThrowOsError(long os_error, const char* file_name) {
    if (os_error != 0) {
        ThrowFileException(FileExceptionCauseFromOsError(os_error), os_error,
            file_name);
    }
}

void ThrowErrno(int error, const char* file_name) {
    if (error != 0) {
        ThrowFileException(FileExceptionCauseFromErrno(error),
            static_cast<long>(*CrtDosErrnoPointer()), file_name);
    }
}

bool CreateIndirect(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent,
    void* init_param) {
    return DialogCreateIndirectCore(dialog, dialog_template, parent,
        init_param, nullptr);
}

void PostModal(MfcDialogCompat& dialog) {
    DialogPostModal(dialog);
}

long HandleSetFont(MfcDialogCompat& dialog, WPARAM font_handle, LPARAM lparam) {
    (void)lparam;
    MfcGdiObjectCompat* font = FontFromHandle(reinterpret_cast<HFONT>(font_handle));
    CWndSetFontInline(dialog, font, TRUE);
    return Default(dialog);
}

// Ghidra original symbol: Catch@00589a0f.
long ProcessWndProcException(MfcWinAppCompat& app, void* exception,
    const MSG* message) {
    if (message == nullptr) {
        return 0;
    }
    if (message->message == WM_CREATE || message->message == WM_PAINT) {
        MSG copy = *message;
        return WinThreadProcessWndProcException(app, exception, &copy);
    }
    return message->message == WM_COMMAND ? 1 : 0;
}

bool SetTemplate(MfcDialogTemplateCompat& dialog_template,
    const DLGTEMPLATE* source, unsigned size) {
    DestroyDialogTemplate(dialog_template);
    if (source == nullptr || size == 0) {
        return false;
    }

    HGLOBAL handle = GlobalAlloc(GMEM_ZEROINIT, size + 0x40);
    if (handle == nullptr) {
        return false;
    }
    void* destination = GlobalLock(handle);
    if (destination == nullptr) {
        GlobalFree(handle);
        return false;
    }
    std::memcpy(destination, source, size);
    GlobalUnlock(handle);

    MfcCStringCompat face_name;
    WORD point_size = 0;
    dialog_template.handle = handle;
    dialog_template.size = size;
    dialog_template.no_font =
        !DialogTemplateGetFont(source, face_name, point_size);
    return true;
}

int IsDirSep(char value) {
    return value == '\\' || value == '/' ? TRUE : FALSE;
}

// Ghidra original symbol: ~CFile.
void CFileDestructor(MfcFileCompat& file) {
    FileDestructor(file);
}

void Abort(MfcFileCompat& file) {
    FileAbort(file);
}

MfcDumpContext& operator<<(MfcDumpContext& context, POINT point) {
    DumpContextWriteString(context, "point(x = ");
    DumpContextWriteLong(context, point.x);
    DumpContextWriteString(context, ", y = ");
    DumpContextWriteLong(context, point.y);
    return DumpContextWriteString(context, ")");
}

MfcDumpContext& operator<<(MfcDumpContext& context, SIZE size) {
    DumpContextWriteString(context, "size(cx = ");
    DumpContextWriteLong(context, size.cx);
    DumpContextWriteString(context, ", cy = ");
    DumpContextWriteLong(context, size.cy);
    return DumpContextWriteString(context, ")");
}

MfcDumpContext& operator<<(MfcDumpContext& context, const RECT& rect) {
    DumpContextWriteString(context, "rect(left = ");
    DumpContextWriteLong(context, rect.left);
    DumpContextWriteString(context, ", top = ");
    DumpContextWriteLong(context, rect.top);
    DumpContextWriteString(context, ", right = ");
    DumpContextWriteLong(context, rect.right);
    DumpContextWriteString(context, ", bottom = ");
    DumpContextWriteLong(context, rect.bottom);
    return DumpContextWriteString(context, ")");
}

MfcDumpContext& operator<<(MfcDumpContext& context,
    const OleCurrencyCompat& value) {
    DumpContextWriteString(context, "COleCurrency Object ");
    DumpContextWriteString(context, "m_status = ");
    DumpContextWriteLong(context, static_cast<long>(value.status));
    DumpContextWriteString(context, " Currency = ");
    std::string text;
    if (FormatOleCurrencyString(value, 0, LOCALE_USER_DEFAULT, text)) {
        DumpContextWriteString(context, text.c_str());
    } else {
        DumpContextWriteLong(context,
            static_cast<long>(value.value.int64 / 10000));
    }
    return context;
}

MfcDumpContext& operator<<(MfcDumpContext& context,
    const OleDateTimeCompat& value) {
    DumpContextWriteString(context, "COleDateTime Object ");
    DumpContextWriteString(context, "m_status = ");
    DumpContextWriteLong(context, static_cast<long>(value.status));
    DumpContextWriteString(context, " DateTime = ");
    std::string text;
    if (FormatOleDateTimeString(value, 0, LOCALE_USER_DEFAULT, text)) {
        DumpContextWriteString(context, text.c_str());
    } else {
        DumpContextWriteDouble(context, value.value);
    }
    return context;
}

MfcArchiveCompat& operator<<(MfcArchiveCompat& archive,
    const OleCurrencyCompat& value) {
    const auto raw = static_cast<unsigned long long>(value.value.int64);
    ArchiveWriteDWordInline(archive, static_cast<DWORD>(value.status));
    ArchiveWriteDWordInline(archive, static_cast<DWORD>(raw >> 32));
    ArchiveWriteLongInline(archive, static_cast<long>(raw & 0xffffffffULL));
    return archive;
}

MfcArchiveCompat& operator>>(MfcArchiveCompat& archive,
    OleCurrencyCompat& value) {
    unsigned status = 0;
    unsigned high = 0;
    long low = 0;
    ArchiveReadDWordInline(archive, status);
    ArchiveReadDWordInline(archive, high);
    ArchiveReadLongInline(archive, low);
    value.status = static_cast<OleDateStatus>(status);
    const unsigned long long raw =
        (static_cast<unsigned long long>(high) << 32) |
        static_cast<unsigned long>(low);
    value.value.int64 = static_cast<long long>(raw);
    return archive;
}

MfcArchiveCompat& operator<<(MfcArchiveCompat& archive,
    const MfcObjectCompat* object) {
    ArchiveWriteObject(archive, const_cast<MfcObjectCompat*>(object));
    return archive;
}

// Ghidra original symbol: ~CDC.
void CDCDestructorOriginal(MfcCDCCompat& dc) {
    CDCDestructor(dc);
}

int PlayMetaFile(MfcCDCCompat& dc, HMETAFILE metafile) {
    if (dc.output_dc == nullptr || metafile == nullptr) {
        return FALSE;
    }
    if (GetDeviceCaps(dc.output_dc, TECHNOLOGY) == DT_METAFILE) {
        return ::PlayMetaFile(dc.output_dc, metafile);
    }
    return EnumMetaFile(dc.output_dc, metafile, CDCMetaFileEnumProc,
        reinterpret_cast<LPARAM>(&dc));
}

// Ghidra original symbol: FID_conflict:CloseEnhanced.
HMETAFILE FID_conflict_CloseEnhanced(MfcCDCCompat& dc) {
    return MetaFileDCClose(dc);
}

HENHMETAFILE CloseEnhanced(MfcCDCCompat& dc) {
    HDC handle = dc.output_dc;
    dc.output_dc = nullptr;
    dc.attribute_dc = nullptr;
    return handle == nullptr ? nullptr : CloseEnhMetaFile(handle);
}

bool CreateEnhanced(MfcCDCCompat& dc, MfcCDCCompat* reference_dc,
    const char* file_name, const RECT* bounds, const char* description) {
    HDC handle = CreateEnhMetaFileA(
        CDCGetSafeHdc(reference_dc), file_name, bounds, description);
    return CDCAttach(dc, handle);
}

HDC GetSafeHdc(const MfcCDCCompat* dc) {
    return CDCGetSafeHdc(dc);
}

MfcCWndCompat* CDCGetWindowInline(MfcCDCCompat& dc) {
    return CDCGetWindow(dc);
}

int CDCIsPrintingInline(const MfcCDCCompat& dc) {
    return CDCIsPrinting(dc) ? TRUE : FALSE;
}

bool CDCCreateCompatibleDCInline(MfcCDCCompat& dc,
    const MfcCDCCompat* source) {
    return CDCCreateCompatibleDC(dc, source);
}

int CDCGetStretchBltModeInline(const MfcCDCCompat& dc) {
    return dc.attribute_dc == nullptr ? 0 : GetStretchBltMode(dc.attribute_dc);
}

void WriteCount(MfcArchiveCompat& archive, unsigned count) {
    ArchiveWriteCount(archive, count);
}

int ReadString(MfcArchiveCompat& archive, MfcCStringCompat& text) {
    return ArchiveReadStringLine(archive, text) ? TRUE : FALSE;
}

void SerializeClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat* runtime_class) {
    if (ArchiveIsStoring(archive)) {
        if (runtime_class != nullptr) {
            ArchiveWriteClass(archive, *runtime_class);
        }
        return;
    }
    (void)ArchiveReadClass(archive, runtime_class);
}

int GetDocString(const MfcDocTemplateCompat& templ, MfcCStringCompat& out,
    int index) {
    std::string value;
    if (!DocTemplateGetDocString(templ, value, index)) {
        out.text.clear();
        return FALSE;
    }
    out.text = value;
    return TRUE;
}

void InitialUpdateFrame(MfcDocTemplateCompat& templ, MfcFrameWndCompat* frame,
    MfcDocumentCompat* document, int make_visible) {
    DocTemplateInitialUpdateFrame(templ, frame, document, make_visible);
}

int SaveAllModified(MfcDocTemplateCompat& templ) {
    return DocTemplateSaveAllModified(templ) ? TRUE : FALSE;
}

void CloseAllDocuments(MfcDocTemplateCompat& templ, int end_session) {
    DocTemplateCloseAllDocuments(templ, end_session != 0);
}

int OnCmdMsg(MfcDocTemplateCompat& templ, UINT id, int code, void* extra,
    MfcCommandHandlerInfoCompat* handler_info) {
    return DocTemplateOnCmdMsg(templ, id, code, extra, handler_info)
        ? TRUE : FALSE;
}

void SetTitle(MfcDocumentCompat& document, const char* title) {
    DocumentSetTitle(document, title);
}

void OnChangedViewList(MfcDocumentCompat& document) {
    DocumentOnChangedViewList(document);
}

int CanCloseFrame(MfcDocumentCompat& document, MfcFrameWndCompat* frame) {
    return DocumentCanCloseFrame(document, frame) ? TRUE : FALSE;
}

void OnFileClose(MfcDocumentCompat& document) {
    DocumentOnFileClose(document);
}

std::size_t GetFirstViewPosition(const MfcDocumentCompat&) {
    return 0;
}

int GetOpenDocumentCount(const MfcDocManagerCompat& manager) {
    return DocManagerGetOpenDocumentCount(&manager);
}

unsigned GetTypeInfoCount(MfcCommandTargetCompat& target) {
    (void)target;
    return CmdTargetGetTypeInfoCountDefault();
}

void WinAppSetCurrentHandles(MfcWinAppCompat& app) {
    app.thread = GetCurrentThread();
    app.thread_id = GetCurrentThreadId();
    if (app.instance == nullptr) {
        app.instance = GetModuleHandleA(nullptr);
    }
    if (app.command_line.empty()) {
        app.command_line = GetCommandLineA();
    }
    AfxSetResourceHandleCompat(app.instance);
}

MfcRuntimeClassCompat* GetCFileRuntimeClassThunk() {
    static MfcRuntimeClassCompat runtime_class{
        "CFile",
        static_cast<int>(sizeof(MfcFileCompat)),
        0xffff,
        nullptr,
        nullptr,
        nullptr};
    runtime_class.base_class = GetCObjectRuntimeClass();
    return &runtime_class;
}

bool DocManagerDeleteShellRegistrationKey(const char* key) {
    return key != nullptr &&
        RegDeleteKeyA(HKEY_CLASSES_ROOT, key) == ERROR_SUCCESS;
}

bool DocManagerSetShellRegistryValue(const char* key,
    const char* value_name, const char* value) {
    if (key == nullptr) {
        return false;
    }
    HKEY opened = nullptr;
    const LSTATUS create_status = RegCreateKeyExA(HKEY_CLASSES_ROOT, key, 0,
        nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr);
    if (create_status != ERROR_SUCCESS) {
        return false;
    }
    const char* text = value == nullptr ? "" : value;
    const DWORD bytes = static_cast<DWORD>(std::strlen(text) + 1);
    const LSTATUS set_status = RegSetValueExA(opened, value_name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(text), bytes);
    RegCloseKey(opened);
    return set_status == ERROR_SUCCESS;
}

void DocManagerUnregisterShellFileTypes(MfcDocManagerCompat& manager) {
    for (MfcDocTemplateCompat* templ : manager.templates) {
        if (templ == nullptr || templ->doc_strings.empty()) {
            continue;
        }
        DocManagerDeleteShellRegistrationKey(templ->doc_strings.c_str());
    }
}

void DocManagerRegisterShellFileTypes(MfcDocManagerCompat& manager,
    BOOL compatibility = TRUE) {
    (void)compatibility;
    for (MfcDocTemplateCompat* templ : manager.templates) {
        if (templ == nullptr || templ->doc_strings.empty()) {
            continue;
        }
        DocManagerSetShellRegistryValue(
            templ->doc_strings.c_str(), nullptr, templ->doc_strings.c_str());
    }
}

int Process(MfcWinAppCompat& app, const char* command_line) {
    if (command_line != nullptr) {
        app.command_line = command_line;
    }
    return TRUE;
}

DWORD GetScrollStyle(const MfcSplitterWndCompat& splitter) {
    return SplitterGetScrollStyle(splitter);
}

void StopTracking(MfcSplitterWndCompat& splitter, int accept) {
    if (accept != 0) {
        SplitterStopTrackingAccept(splitter);
    } else {
        SplitterStopTrackingCancel(splitter);
    }
}

void OnDisplayChange(MfcSplitterWndCompat& splitter) {
    if (CWndIsIconicInline(splitter) == FALSE &&
        CWndIsWindowVisibleInline(splitter) != FALSE) {
        SplitterRecalcLayout(splitter);
    }
}

void OnLButtonDown(MfcSplitterWndCompat& splitter, UINT flags, POINT point) {
    (void)flags;
    if (!splitter.tracking) {
        StartTracking(splitter, SplitterHitTest(splitter, point));
    }
}

HBRUSH OnCtlColor(MfcControlBarCompat& bar, MfcCDCCompat* dc,
    MfcCWndCompat* child, UINT type) {
    HDC handle = dc == nullptr ? nullptr : dc->output_dc;
    HWND child_handle = child == nullptr ? nullptr : child->window;
    return reinterpret_cast<HBRUSH>(
        CWndOnCtlColor(bar, handle, child_handle, type));
}

// Ghidra original symbol: FID_conflict:CMFCToolBarCmdUI.
MfcCmdUICompat& FID_conflict_CMFCToolBarCmdUI(MfcCmdUICompat& cmd_ui) {
    CCmdUI(cmd_ui);
    cmd_ui.kind = MfcCmdUIKind::ToolBar;
    return cmd_ui;
}

// Ghidra original symbol: ~CDialogBar.
void CDialogBarDestructorOriginal(MfcDialogBarCompat& bar) {
    DestroyDialogBar(bar);
}

void HideApplication(MfcWinAppCompat& app) {
    MfcCWndCompat* main = app.main_window == nullptr
        ? nullptr : CWndFromHandle(app.main_window);
    if (main == nullptr) {
        return;
    }
    CWndShowWindow(*main, SW_HIDE);
    CWndShowOwnedPopupsInline(*main, FALSE);
    CWndSetWindowPos(*main, HWND_BOTTOM, 0, 0, 0, 0,
        SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

long OnSizeParent(MfcDockBarCompat& dock_bar, UINT message,
    MfcSizeParentParamsCompat& layout) {
    return ControlBarOnSizeParent(dock_bar, message, layout);
}

// Ghidra original symbol: ~CMiniDockFrameWnd.
void CMiniDockFrameWndDestructorOriginal(MfcMiniDockFrameWndCompat& frame) {
    DestroyMiniDockFrameWnd(frame);
}

MfcNewTypeDlgCompat& CNewTypeDlg(MfcNewTypeDlgCompat& dialog,
    const std::vector<MfcDocTemplateCompat*>* templates) {
    return ConstructNewTypeDlg(dialog, templates);
}

void Cleanup(MfcPropertyPageCompat& page) {
    PropertyPageCleanup(page);
}

int OnApply(MfcPropertyPageCompat& page) {
    return PropertyPageOnApply(page);
}

int OnSetActive(MfcPropertyPageCompat& page) {
    return PropertyPageOnSetActive(page);
}

int IsButtonEnabled(MfcPropertyPageCompat& page, int button_id) {
    return PropertyPageIsButtonEnabled(page, button_id) ? TRUE : FALSE;
}

MfcPropertyPageCompat* GetActivePage(MfcPropertySheetCompat& sheet) {
    return PropertySheetGetActivePage(sheet);
}

int PrintSelection(const MfcPrintDialogExCompat& dialog) {
    return (dialog.result_action & 1U) != 0 ? TRUE : FALSE;
}

void ContinueRouting(MfcCmdUICompat& cmd_ui) {
    cmd_ui.continue_routing = true;
}

MfcImageListCompat* DeleteImageListOrMenuScalarDtor(
    MfcImageListCompat* image_list, unsigned flags) {
    if (image_list == nullptr) {
        return nullptr;
    }
    FID_conflict_DestructImageList(*image_list);
    if ((flags & 1U) != 0U) {
        delete image_list;
    }
    return image_list;
}

MfcMenuCompat* DeleteImageListOrMenuScalarDtor(MfcMenuCompat* menu,
    unsigned flags) {
    if (menu == nullptr) {
        return nullptr;
    }
    DestroyMenuCompat(*menu);
    if ((flags & 1U) != 0U) {
        delete menu;
    }
    return menu;
}

// Ghidra original symbol: operator_struct_HWND__*.
HWND operator_struct_HWND(const MfcCWndCompat* window) {
    return window == nullptr ? nullptr : window->window;
}

MfcCWndCompat* GetOwner(MfcCWndCompat& window) {
    if (window.window == nullptr) {
        return nullptr;
    }
    HWND owner = GetWindow(window.window, GW_OWNER);
    if (owner != nullptr) {
        return CWndFromHandle(owner);
    }
    return CWndGetParentInline(window);
}

void SetOwner(MfcCWndCompat& window, MfcCWndCompat* owner) {
    if (window.window == nullptr) {
        return;
    }
    SetWindowLongPtrA(window.window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(owner == nullptr ? nullptr : owner->window));
}

POINT GetCaretPos() {
    POINT point{};
    ::GetCaretPos(&point);
    return point;
}

int OnEraseBkgnd(MfcCWndCompat& window, MfcCDCCompat*) {
    return static_cast<int>(Default(window));
}

void OnGetMinMaxInfo(MfcCWndCompat& window, MINMAXINFO*) {
    (void)Default(window);
}

void CWaitCursor() {
    AfxBeginWaitCursor();
}

MfcCDCCompat& CMetaFileDC(MfcCDCCompat& dc) {
    dc = MfcCDCCompat{};
    dc.runtime_class = MfcExceptionRuntimeThunk_005e8ca8();
    return dc;
}

MfcFrameWndCompat& CMDIChildWnd(MfcFrameWndCompat& frame) {
    return ConstructFrameWnd(frame);
}

void _AFX_BASE_MODULE_STATE() {
    InitializeMfcBaseModuleStateCompat(true);
}
void* _AFX_COLOR_STATE(void* state) {
    if (state != nullptr) {
        auto* bytes = static_cast<unsigned char*>(state);
        const std::uint32_t white = 0x00ffffff;
        for (std::size_t index = 0; index < 16; ++index) {
            std::memcpy(bytes + 4 + index * sizeof(white), &white,
                sizeof(white));
        }
    }
    return state;
}

// Ghidra original symbol: ~_AFX_DEBUG_STATE.
void AfxDebugStateDestructorOriginal(AfxDebugState* state) {
    DestroyProcessLocalAfxDebugState(state);
}

// Ghidra original symbol: FID_conflict:_AFXCTL_AMBIENT_CACHE.
void* FID_conflict_AFXCTL_AMBIENT_CACHE(void* cache) {
    if (cache != nullptr) {
        ConstructCObject(*static_cast<MfcObjectCompat*>(cache));
    }
    return cache;
}

// Ghidra original symbol: ~CThreadLocalObject.
void CThreadLocalObjectDestructorOriginal(void* local) {
    if (local == nullptr) {
        return;
    }
    int slot = 0;
    std::memcpy(&slot, local, sizeof(slot));
    MfcThreadSlotRuntimeDeleteSlot(slot);
    slot = 0;
    std::memcpy(local, &slot, sizeof(slot));
}

// Ghidra original symbol: ~_AFX_PROPPAGEFONTINFO.
void AfxPropPageFontInfoDestructorOriginal(void* info) {
    DestroyPropPageFontInfoCompat(info);
}

// Ghidra original symbol: ~CWinThread.
void CWinThreadDestructorOriginal(MfcWinThreadCompat& thread) {
    DestroyWinThreadCompat(thread);
}

// Ghidra original symbol: ~AUX_DATA.
void AuxDataDestructorOriginal(void*) {}

void MfcGdiObjectRuntimeTail_005ffe50(MfcGdiObjectCompat& object) {
    DestroyGdiObjectCompat(object);
}

// Ghidra original symbol: ~CDocManager.
void CDocManagerDestructorOriginal(MfcDocManagerCompat& manager) {
    DestroyDocManager(manager);
}

MfcPrintInfoCompat* PrintInfoIdentityInline(MfcPrintInfoCompat* info) {
    return info;
}

void DestroyDialogInlineThunk(MfcDialogCompat& dialog) {
    DestroyDialog(dialog);
}

MfcDialogCompat* DeleteDialogScalarDtorThunk(MfcDialogCompat* dialog,
    unsigned flags) {
    return DeleteDialogScalarDtor(dialog, flags);
}

// Ghidra original symbol: ~CBitmapButton.
void CBitmapButtonDestructorOriginal(MfcBitmapButtonCompat& button) {
    CBitmapButtonDestructor(button);
}

// Ghidra original symbol: ~CToolBar.
void ToolBarDestructorOriginal(MfcToolBarCompat& toolbar) {
    if (toolbar.image_well != nullptr) {
        DeleteObject(toolbar.image_well);
        toolbar.image_well = nullptr;
    }
    DestroyControlBar(toolbar);
}

MfcCWndCompat* SplitterVirtualCallE8Inline(MfcSplitterWndCompat& splitter,
    int* row, int* column) {
    return SplitterGetActivePane(splitter, row, column);
}

RECT GetBorders(const MfcControlBarCompat& bar) {
    return RECT{bar.cx_left_border, bar.cy_top_border,
        bar.cx_right_border, bar.cy_bottom_border};
}

bool ToolBarLoadBitmapResourceIdInline(MfcToolBarCompat& toolbar,
    UINT resource_id) {
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(LoadImageA(
        AfxGetResourceHandleCompat(), MAKEINTRESOURCEA(resource_id & 0xffffU),
        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (bitmap == nullptr) {
        bitmap = LoadBitmapA(GetModuleHandleA(nullptr),
            MAKEINTRESOURCEA(resource_id & 0xffffU));
    }
    if (bitmap == nullptr) {
        return false;
    }
    ToolBarSetBitmapHandle(toolbar, bitmap);
    return true;
}

bool ToolBarLoadToolBarResourceIdInline(MfcToolBarCompat& toolbar,
    UINT resource_id) {
    HINSTANCE instance = AfxGetResourceHandleCompat();
    toolbar.image_instance = instance;
    toolbar.image_resource = FindResourceA(instance,
        MAKEINTRESOURCEA(resource_id & 0xffffU), MAKEINTRESOURCEA(241));
    return ToolBarLoadBitmapResourceIdInline(toolbar, resource_id) ||
        toolbar.image_resource != nullptr;
}

bool ToolBarLoadBitmapResource(MfcToolBarCompat& toolbar,
    LPCSTR resource_name) {
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(LoadImageA(
        AfxGetResourceHandleCompat(), resource_name, IMAGE_BITMAP, 0, 0,
        LR_CREATEDIBSECTION));
    if (bitmap == nullptr) {
        bitmap = LoadBitmapA(GetModuleHandleA(nullptr), resource_name);
    }
    if (bitmap == nullptr) {
        return false;
    }
    ToolBarSetBitmapHandle(toolbar, bitmap);
    return true;
}

bool ToolBarLoadBitmapResource(MfcToolBarCompat& toolbar, UINT resource_id) {
    return ToolBarLoadBitmapResourceIdInline(toolbar, resource_id);
}

bool ToolBarLoadToolBarResource(MfcToolBarCompat& toolbar,
    LPCSTR resource_name) {
    HINSTANCE instance = AfxGetResourceHandleCompat();
    toolbar.image_instance = instance;
    toolbar.image_resource =
        FindResourceA(instance, resource_name, MAKEINTRESOURCEA(241));
    return ToolBarLoadBitmapResource(toolbar, resource_name) ||
        toolbar.image_resource != nullptr;
}

bool ToolBarLoadToolBarResource(MfcToolBarCompat& toolbar, UINT resource_id) {
    return ToolBarLoadToolBarResourceIdInline(toolbar, resource_id);
}

SIZE CSize() {
    return SIZE{};
}

// Ghidra original symbols: FID_conflict:operator+, FID_conflict:operator-,
// operator-.
POINT GeometryAdd(POINT point, SIZE size) {
    return POINT{point.x + size.cx, point.y + size.cy};
}

POINT GeometrySubtract(POINT point, SIZE size) {
    return POINT{point.x - size.cx, point.y - size.cy};
}

SIZE GeometryDifference(POINT left, POINT right) {
    return SIZE{left.x - right.x, left.y - right.y};
}

void Offset(POINT& point, POINT delta) {
    point.x += delta.x;
    point.y += delta.y;
}

void FrameWndOnUpdateContextHelp(MfcFrameWndCompat& frame,
    MfcCmdUICompat& cmd_ui) {
    if (AfxGetMainWndCompat() == &frame) {
        CmdUIEnable(cmd_ui, frame.in_help_mode);
        return;
    }
    cmd_ui.continue_routing = true;
}

void BringToTop(MfcFrameWndCompat& frame, int show_command) {
    if (show_command == SW_HIDE || show_command == SW_MINIMIZE ||
        show_command == SW_SHOWMINNOACTIVE ||
        show_command == SW_SHOWNOACTIVATE || show_command == SW_SHOWNA) {
        return;
    }
    if (frame.window == nullptr || !IsWindow(frame.window)) {
        return;
    }
    HWND popup = GetLastActivePopup(frame.window);
    BringWindowToTop(popup == nullptr ? frame.window : popup);
}

void ActivateFrame(MfcFrameWndCompat& frame, int show_command) {
    if (show_command == -1) {
        if (CWndIsWindowVisibleInline(frame) == FALSE) {
            show_command = SW_SHOWNORMAL;
        } else if (CWndIsIconicInline(frame) != FALSE) {
            show_command = SW_RESTORE;
        }
    }
    BringToTop(frame, show_command);
    if (show_command != -1) {
        CWndShowWindow(frame, show_command);
        BringToTop(frame, show_command);
    }
}

int GetPageIndex(MfcPropertySheetCompat& sheet, MfcPropertyPageCompat* page) {
    const int count = PropertySheetGetPageCount(sheet);
    for (int index = 0; index < count; ++index) {
        if (PropertySheetGetPageAt(sheet, index) == page) {
            return index;
        }
    }
    return -1;
}

void OnClose(MfcPropertySheetCompat& sheet) {
    if (sheet.modeless) {
        CWndDestroyWindow(sheet);
        return;
    }
    CWndEndModalLoop(sheet, IDCANCEL);
    if (sheet.window != nullptr && IsWindow(sheet.window)) {
        SendMessageA(sheet.window, PSM_PRESSBUTTON, PSBTN_CANCEL, 0);
    }
}

int Track(MfcRectTrackerCompat& tracker, MfcCWndCompat& window, POINT point,
    BOOL allow_invert, MfcCWndCompat* clip_window) {
    (void)allow_invert;
    const int hit = RectTrackerHitTestHandles(tracker, point);
    if (hit < 0) {
        return FALSE;
    }
    return RectTrackerTrackHandle(tracker, hit, window, point, clip_window)
        ? TRUE
        : FALSE;
}

int TrackRubberBand(MfcRectTrackerCompat& tracker, MfcCWndCompat& window,
    POINT point, BOOL allow_invert) {
    (void)allow_invert;
    SetRect(&tracker.rect, point.x, point.y, point.x, point.y);
    return RectTrackerTrackHandle(tracker, 2, window, point, nullptr)
        ? TRUE
        : FALSE;
}

void CheckListBoxMeasureItemDefault(MfcCheckListBoxCompat& control) {
    if (control.window == nullptr || !IsWindow(control.window)) {
        return;
    }
    const DWORD style = static_cast<DWORD>(
        GetWindowLongA(control.window, GWL_STYLE));
    if ((style & (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) !=
        (LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)) {
        AfxTraceOutput("CCheckListBox default MeasureItem requires "
            "owner-draw fixed strings.\n");
    }
}

int FrameWndOnCreateThunk(MfcFrameWndCompat& frame, CREATESTRUCTA* create) {
    auto* context = create == nullptr ? nullptr :
        static_cast<MfcCreateContextCompat*>(create->lpCreateParams);
    return FrameWndOnCreate(frame, create, context);
}

MfcFrameWndCompat* GetParentFrame(MfcCWndCompat& window) {
    for (HWND parent = window.window == nullptr ? nullptr : GetParent(window.window);
         parent != nullptr; parent = GetParent(parent)) {
        MfcCWndCompat* candidate = CWndFromHandlePermanent(parent);
        if (candidate == nullptr || candidate->runtime_class == nullptr) {
            continue;
        }
        const char* name = candidate->runtime_class->class_name;
        if (name != nullptr && std::strstr(name, "Frame") != nullptr) {
            return static_cast<MfcFrameWndCompat*>(candidate);
        }
    }
    return nullptr;
}

MfcCWndCompat* GetSafeOwner(MfcCWndCompat* parent, HWND* disabled_owner) {
    HWND parent_handle = parent == nullptr ? nullptr : parent->window;
    return CWndFromHandle(AfxGetSafeOwnerCompat(parent_handle, disabled_owner));
}

MfcCWndCompat* GetDescendantWindow(HWND parent, int control_id,
    int only_permanent) {
    if (parent == nullptr) {
        return nullptr;
    }
    if (HWND child = GetDlgItem(parent, control_id)) {
        if (MfcCWndCompat* nested =
            GetDescendantWindow(child, control_id, only_permanent)) {
            return nested;
        }
        return only_permanent != 0 ? CWndFromHandlePermanent(child)
            : CWndFromHandle(child);
    }
    for (HWND child = GetTopWindow(parent); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (MfcCWndCompat* nested =
            GetDescendantWindow(child, control_id, only_permanent)) {
            return nested;
        }
    }
    return nullptr;
}

void SendMessageToDescendants(HWND parent, UINT message, WPARAM wparam,
    LPARAM lparam, int deep, int only_permanent) {
    for (HWND child = GetTopWindow(parent); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (only_permanent != 0) {
            if (MfcCWndCompat* wrapper = CWndFromHandlePermanent(child)) {
                AfxCallWndProc(*wrapper, child, message, wparam, lparam);
            }
        } else {
            SendMessageA(child, message, wparam, lparam);
        }
        if (deep != 0 && GetTopWindow(child) != nullptr) {
            SendMessageToDescendants(child, message, wparam, lparam, deep,
                only_permanent);
        }
    }
}

// Ghidra original symbol: FID_conflict:SetScrollPos.
int FID_conflict_SetScrollPos(MfcCWndCompat& window, int bar, int position,
    BOOL redraw) {
    return CWndSetScrollPosCompat(window, bar, position, redraw);
}

BOOL FID_conflict_SetScrollPos(MfcCWndCompat& window, int bar,
    int* min_position, int* max_position) {
    CWndGetScrollRangeCompat(window, bar, min_position, max_position);
    return window.window != nullptr ? TRUE : FALSE;
}

int SendChildNotifyLastMsg(MfcCWndCompat& window, LRESULT* result) {
    return CWndSendChildNotifyLastMsgByHandle(window.window, result) ? TRUE : FALSE;
}

// Ghidra original symbol: FID_conflict:OnHScroll.
void FID_conflict_OnHScroll(MfcCWndCompat& window, UINT, UINT,
    MfcScrollBarCompat* scroll_bar) {
    if (scroll_bar == nullptr ||
        !CWndSendChildNotifyLastMsgByHandle(scroll_bar->window, nullptr)) {
        Default(window);
    }
}

void FID_conflict_OnHScroll(MfcCWndCompat& window, UINT, UINT,
    MfcCWndCompat* scroll_bar) {
    if (scroll_bar == nullptr ||
        !CWndSendChildNotifyLastMsgByHandle(scroll_bar->window, nullptr)) {
        Default(window);
    }
}

void OnEnterIdle(MfcCWndCompat& window, UINT, MfcCWndCompat*) {
    MSG message{};
    while (PeekMessageA(&message, nullptr, WM_ENTERIDLE, WM_ENTERIDLE,
        PM_REMOVE) != FALSE) {
        DispatchMessageA(&message);
    }
    Default(window);
}
#endif

void InitializeLegacyAsyncTcpSocketBase(LegacyAsyncTcpSocket& socket_state) {
    InitializeLegacyAsyncTcpSocket(socket_state);
}

void DestroyLegacyAsyncTcpSocket(LegacyAsyncTcpSocket& socket_state) {
    DeleteLegacyAsyncTcpSocket(socket_state);
}

#ifdef _WIN32
void MemoStaticResourceWrapper00() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper01() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper02() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper03() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper04() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper05() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper06() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper07() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper08() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper09() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper10() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper11() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper12() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper13() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper14() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper15() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper16() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper17() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper18() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper19() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper20() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper21() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper22() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper23() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper24() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper25() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper26() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper27() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper28() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper29() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper30() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper31() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper32() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper33() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper34() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper35() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper36() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper37() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper38() { MemoStaticResourceWrapperNN(); }
void MemoStaticResourceWrapper39() { MemoStaticResourceWrapperNN(); }
#endif

int CrtFcloseUnlocked(FILE* stream) {
    return stream != nullptr ? std::fclose(stream) : EOF;
}

void LockCrtExit() {
    LockCrtRuntime(0x0d);
}

void UnlockCrtExit() {
    UnlockCrtRuntime(0x0d);
}

void* CrtMallocWithHeapContext(std::size_t size) {
    return CrtMallocRetry(size);
}

void UnlockCrtHeapAfterMalloc() {
    UnlockCrtRuntime(9);
}
void FinishCrtMallocRetry() {}

void* CrtDebugAllocBlock(std::size_t size) {
    return CrtDebugHeapAlloc(size);
}

void UnlockCrtHeapAfterExpand() {
    UnlockCrtRuntime(9);
}
void FinishCrtExpand() {}

void* CrtDebugHeapReallocOrExpand(void* memory, std::size_t new_size,
    bool allow_move) {
    return CrtReallocOrExpand(memory, new_size, allow_move);
}

void UnlockCrtHeapAfterRealloc() {
    UnlockCrtRuntime(9);
}
void FinishCrtRealloc() {}

void CrtDebugFreeNormal(void* memory) {
    CrtDebugHeapFree(memory);
}

void UnlockCrtHeapAfterFree() {
    UnlockCrtRuntime(9);
}
void FinishCrtFree() {}
void UnlockCrtHeapAfterMemorySize() {
    UnlockCrtRuntime(9);
}
void FinishCrtMemorySize() {}

long __CrtSetBreakAlloc(long allocation) {
    return CrtSetBreakAlloc(allocation);
}

void UnlockCrtHeapAfterSetBlockType() {
    UnlockCrtRuntime(9);
}
void FinishCrtSetBlockType() {}

void* __CrtSetAllocHook(void* hook) {
    return reinterpret_cast<void*>(CrtSetAllocHook(
        reinterpret_cast<CrtAllocHookCallback>(hook)));
}

void UnlockCrtHeapAfterCheckMemory() {
    UnlockCrtRuntime(9);
}
void FinishCrtCheckMemory() {}
void UnlockCrtHeapAfterClientObjects() {
    UnlockCrtRuntime(9);
}
void FinishCrtClientObjects() {}
void UnlockCrtHeapAfterIsMemoryBlock() {
    UnlockCrtRuntime(9);
}
void FinishCrtIsMemoryBlock() {}
void UnlockCrtHeapAfterMemCheckpoint() {
    UnlockCrtRuntime(9);
}
void FinishCrtMemCheckpoint() {}
void UnlockCrtHeapAfterDumpObjects() {
    UnlockCrtRuntime(9);
}
void FinishCrtDumpObjects() {
    if (CrtDbgReport(0, nullptr, 0, nullptr, "Object dump complete.\n") == 1) {
        CrtDebugBreak();
    }
}

void NoopCrtFloatingPointInit() {}

double CrtSinEntry(double value) { return CrtSin(value); }
double CrtSinFloadFallback(double value) { return CrtSin(value); }
double CrtSinCore(double value) { return CrtSin(value); }
double CrtCosEntry(double value) { return CrtCos(value); }
double CrtCosFloadFallback(double value) { return CrtCos(value); }
double CrtCosCore(double value) { return CrtCos(value); }
double CrtAtanEntry(double value) { return CrtAtan(value); }
double CrtAtanFloadFallback(double value) { return CrtAtan(value); }
double CrtAtanCore(double value) { return CrtAtan(value); }

int HandleCrtRenamePath(const char* old_path, const char* new_path) {
#ifdef _WIN32
    const BOOL moved = MoveFileA(old_path, new_path);
    const DWORD error = moved != FALSE ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_SUCCESS) {
        return 0;
    }
    __dosmaperr(error);
    return -1;
#else
    return std::rename(old_path, new_path);
#endif
}

void* __CrtSetReportFile(int report_type, void* file) {
    return CrtSetReportFile(report_type, file);
}

void CheckStackPointerAlias() {}
// Ghidra original symbol: __chkesp.

void store_dt(std::string& out, const std::tm& value) {
    FormatLocaleDateTimePattern("%x", value, out, LocaleTimeData{});
}

// Ghidra original symbol: FID_conflict:__expand.
void* FID_conflict___expand(void* memory, std::size_t new_size) {
    return CrtExpand(memory, new_size);
}

// Ghidra original symbol: FID_conflict:__realloc_dbg.
void* FID_conflict___realloc_dbg(void* memory, std::size_t size,
    int block_type = 1, const char* file_name = nullptr,
    int line_number = 0) {
    (void)block_type;
    (void)file_name;
    (void)line_number;
    return __realloc_dbg(memory, size);
}

// Ghidra original symbol: FID_conflict:__CrtSetDumpClient.
void* FID_conflict___CrtSetDumpClient(void* client) {
    return reinterpret_cast<void*>(CrtSetDumpClient(
        reinterpret_cast<CrtDumpClientCallback>(client)));
}

// Ghidra emitted the report-hook setter under the same FID-conflict name at 005260a0.
void* FID_conflict___CrtSetReportHook(void* hook) {
    return reinterpret_cast<void*>(CrtSetReportHook(
        reinterpret_cast<CrtReportHookCallback>(hook)));
}

// Ghidra original symbol: `eh_vector_constructor_iterator'.
void eh_vector_constructor_iterator(void* first, std::size_t element_size,
    int element_count, EhObjectCallback constructor,
    EhObjectCallback destructor) {
    EhVectorConstructorIterator(
        first, element_size, element_count, constructor, destructor);
}

// Ghidra original symbol: FID_conflict:___CxxFrameHandler3.
int FID_conflict____CxxFrameHandler3(void* exception_record,
    void* registration, void* context, void* dispatcher_context) {
    return CxxFrameHandler(exception_record, registration, context,
        dispatcher_context, nullptr, 0, nullptr, false);
}

// Ghidra original symbol: ___CxxLongjmpUnwind@4.
void CxxLongjmpUnwindOriginal(void*) {}

// Ghidra original symbol: FID_conflict:_remove.
int FID_conflict__remove(const char* path) {
    return CrtRemovePath(path);
}

// Ghidra original symbol: FID_conflict:__utime.
int FID_conflict___utime(const char* path, const void* times) {
#ifdef _WIN32
    return path == nullptr ? -1
        : _utime(path, const_cast<_utimbuf*>(
              static_cast<const _utimbuf*>(times)));
#else
    (void)path;
    (void)times;
    return 0;
#endif
}

// Ghidra original symbol: __seh_longjmp_unwind@4.
void SehLongjmpUnwindOriginal(void*) {}

// Ghidra original symbol: type_info::operator!=.
bool type_info_operator_not_equal(const void* lhs, const void* rhs) {
    return lhs != rhs;
}

// Ghidra original symbol: __CxxThrowException@8.
void CxxThrowExceptionOriginal(void*, void*) {
    std::abort();
}

void CrtEndThreadExAlias(unsigned exit_code) {
    // Ghidra original symbol: __endthreadex.
#ifdef _WIN32
    AfxEndThreadCompat(exit_code, true);
#else
    (void)exit_code;
#endif
}

// Ghidra original symbol: FID_conflict:__ctime64.
char* FID_conflict___ctime64(const long long* value) {
    if (value == nullptr) {
        return nullptr;
    }
    const std::time_t converted = static_cast<std::time_t>(*value);
    return std::ctime(&converted);
}

// Ghidra original symbol: FID_conflict:_store_str.
void FID_conflict__store_str(std::string& out, const char* text) {
    if (text != nullptr) {
        out += text;
    }
}

// Ghidra original symbol: FID_conflict:_store_number.
void FID_conflict__store_number(std::string& out, int value,
    unsigned width, bool suppress_padding) {
    StoreStrftimePaddedNumber(value, width, out, suppress_padding);
}

// Ghidra original symbol: FID_conflict:__set_errno_from_matherr.
int FID_conflict___set_errno_from_matherr(int math_type) {
    int* err = CrtErrnoPointer();
    if (math_type == 1) {
        *err = EDOM;
    } else if (math_type > 1 && math_type < 4) {
        *err = ERANGE;
    }
    return *err;
}

// Ghidra original symbol: FID_conflict:___AdjustPointer.
void* FID_conflict____AdjustPointer(void* object, long displacement) {
    return object == nullptr ? nullptr
        : static_cast<void*>(static_cast<char*>(object) + displacement);
}

// Ghidra original symbol: __CallSettingFrame@12.
void CallSettingFrameOriginal(void*, void*, void*) {}

// Ghidra original symbol: FID_conflict:__open.
int FID_conflict___open(const char* path, int open_flags,
    int permission = 0) {
    return CrtOpenFileDescriptor(path, static_cast<unsigned>(open_flags), 0,
        static_cast<unsigned>(permission));
}

// Ghidra original symbol: FID_conflict:__getenv_lk.
char* FID_conflict___getenv_lk(const char* name) {
    return __getenv_lk(name);
}

#ifdef _WIN32
void AfxCheckError(HRESULT result) {
    if (FAILED(result)) {
        if (result == E_OUTOFMEMORY) {
            ThrowMfcMemoryException();
        }
        MfcOleRuntime_005f4b9a(result);
    }
}
#else
void AfxCheckError(long result) {
    if (result < 0) {
        if (result == static_cast<long>(
                static_cast<std::int32_t>(0x8007000eUL))) {
            ThrowMfcMemoryException();
        }
        MfcOleRuntime_005f4b9a(static_cast<SCODE>(result));
    }
}
#endif

namespace {

using AfxAllocHookCallback = int (*)(unsigned size, int client_block,
    long request_number);

struct AfxDoForAllObjectsProxyContext {
    CrtClientObjectCallback callback = nullptr;
    void* context = nullptr;
};

AfxAllocHookCallback g_afx_alloc_hook = nullptr;
CrtAllocHookCallback g_previous_crt_alloc_hook = nullptr;
bool g_afx_alloc_hook_installed = false;
thread_local AfxExceptionContextCompat g_afx_exception_context;

int AfxAllocHookBridgeCompat(int alloc_type, void* user_data,
    std::size_t size, int block_type, long request_number,
    const char* file_name, int line_number) {
    if (alloc_type == 1 && g_afx_alloc_hook != nullptr &&
        g_afx_alloc_hook(static_cast<unsigned>(size),
            (block_type & 0xffff) == 4, request_number) == 0) {
        return 0;
    }
    if (g_previous_crt_alloc_hook != nullptr) {
        return g_previous_crt_alloc_hook(alloc_type, user_data, size,
            block_type, request_number, file_name, line_number);
    }
    return 1;
}

} // namespace

AfxExceptionContextCompat& AfxExceptionContextCompatState() {
    return g_afx_exception_context;
}

void* AfxSetAllocHook(void* hook) {
    if (!g_afx_alloc_hook_installed) {
        g_previous_crt_alloc_hook =
            CrtSetAllocHook(AfxAllocHookBridgeCompat);
        g_afx_alloc_hook_installed = true;
    }
    AfxAllocHookCallback previous = g_afx_alloc_hook;
    g_afx_alloc_hook = reinterpret_cast<AfxAllocHookCallback>(hook);
    return reinterpret_cast<void*>(previous);
}

void AfxSetAllocStop(long allocation) {
    __CrtSetBreakAlloc(allocation);
}

bool AfxIsMemoryBlock(const void* memory, std::size_t size,
    long* request_number) {
    return __CrtIsMemoryBlock(memory, size, request_number, nullptr, nullptr);
}

bool AfxIsMemoryBlock(const void* memory, std::size_t size) {
    return AfxIsMemoryBlock(memory, size, nullptr);
}

void* CMemoryState(void* state) {
    if (state != nullptr) {
        std::memset(state, 0, 100);
    }
    return state;
}

void _AfxDoForAllObjectsProxy(void* memory, void* context) {
    auto* proxy = static_cast<AfxDoForAllObjectsProxyContext*>(context);
    if (proxy != nullptr && proxy->callback != nullptr) {
        proxy->callback(memory, proxy->context);
    }
}

void AfxThrowLastCleanup() {
    AfxExceptionContextCompat& context = AfxExceptionContextCompatState();
    if (context.current != nullptr) {
        context.current->exception = nullptr;
    }
}

void AfxThrowResourceException() {
    ThrowMfcResourceException();
}

void* AfxGetExceptionContext() {
    return &AfxExceptionContextCompatState();
}

MfcRuntimeClassCompat* AfxClassInit(MfcRuntimeClassCompat* runtime_class) {
    return AfxClassInitObject(runtime_class);
}

int _AfxHandleSetCursor(MfcCWndCompat* window, UINT hit_test, UINT message) {
    return AfxHandleSetCursor(window, hit_test, message);
}

MfcMenuCompat* _AfxFindPopupMenuFromID(MfcMenuCompat* menu, UINT command_id) {
    return AfxFindPopupMenuFromID(menu, command_id);
}

void AfxLockTempMaps() {
    AfxLockTempMapsCompat();
}

#ifdef _WIN32
INT_PTR CALLBACK AfxDlgProc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return AfxDlgProcCompat(hwnd, message, wparam, lparam);
}
#endif

int AfxMessageBox(const char* prompt, UINT type, UINT id_help) {
    return AfxMessageBoxCompat(prompt, type, id_help);
}

void _AfxAdjustRectangle(RECT& rect, POINT point) {
    AfxAdjustRectangle(rect, point);
}

bool AfxAssertFailedLine(const char* file, int line) {
#ifdef _WIN32
    MSG quit_message{};
    const BOOL had_quit = PeekMessageA(&quit_message, nullptr, WM_QUIT,
        WM_QUIT, PM_REMOVE);
#endif
    const int result = CrtDbgReport(2, file, line, nullptr, nullptr);
#ifdef _WIN32
    if (had_quit != FALSE) {
        PostQuitMessage(static_cast<int>(quit_message.wParam));
    }
#endif
    return result != 0;
}

BOOL AfxExtCDCVirtualCall5CInline(MfcCDCCompat& dc, int x, int y) {
    if (dc.output_dc == nullptr && AfxAssertFailedLine("afxext.inl", 0x30)) {
        CrtDebugBreak();
        return FALSE;
    }
    return CDCPtVisible(dc, x, y);
}

void AfxExtCDCVirtualCall5CInline() {}
void* AfxExtIdentityInline005d50d4(void* value) { return value; }
void* AfxExtIdentityInline005d510d(void* value) { return value; }
void AfxExtAssertUnsupportedLine60() {
    if (AfxAssertFailedLine("afxext.inl", 0x60)) {
        CrtDebugBreak();
    }
}
void* AfxExtIdentityInline005d51c6(void* value) { return value; }
void AfxExtAssertUnsupportedLine69() {
    if (AfxAssertFailedLine("afxext.inl", 0x69)) {
        CrtDebugBreak();
    }
}

MfcWinAppCompat* AfxGetApp() {
    return AfxGetAppCompat();
}

MfcCWndCompat* AfxGetMainWnd() {
    return AfxGetMainWndCompat();
}

#ifdef _WIN32
void AfxTermLocalData(HINSTANCE instance, int process_terminating) {
    AfxTermLocalDataCompat(instance, process_terminating != 0);
}
#else
void AfxTermLocalData(void*, int) {}
#endif

#ifdef _WIN32
bool AfxWinInitCompat(HINSTANCE instance, HINSTANCE previous_instance,
    LPSTR command_line, int command_show) {
    if (previous_instance != nullptr &&
        AfxAssertFailedLine("appinit.cpp", 0x1b)) {
        CrtDebugBreak();
    }
    const UINT old_error_mode = SetErrorMode(0);
    SetErrorMode(old_error_mode | SEM_FAILCRITICALERRORS |
        SEM_NOOPENFILEERRORBOX);

    if (instance == nullptr) {
        instance = GetModuleHandleA(nullptr);
    }
    AfxSetResourceHandleCompat(instance);

    MfcWinAppCompat* app = AfxGetAppCompat();
    if (app != nullptr) {
        app->instance = instance;
        app->previous_instance = previous_instance;
        app->command_line = command_line == nullptr ? "" : command_line;
        app->command_show = command_show;
        WinAppSetCurrentHandles(*app);
        AfxWinInitThread(*app);
    }
    return true;
}
#endif

std::string AfxGetFileNameCompat(MfcFileCompat& file) {
    return FileGetFileName(file);
}

int AfxGetFileNameCompat(const char* path, char* output, int output_chars) {
    if (path == nullptr) {
        if (AfxAssertFailedLine("appinit.cpp", 0x8d)) {
            CrtDebugBreak();
        }
        if (output != nullptr && output_chars > 0) {
            output[0] = '\0';
        }
        return 0;
    }

    const char* file_name = path;
    for (const char* cursor = path; *cursor != '\0';
         cursor = AdvanceMbcsStringPointer(cursor)) {
        if (*cursor == '\\' || *cursor == '/' || *cursor == ':') {
            file_name = AdvanceMbcsStringPointer(cursor);
        }
    }

    if (output == nullptr) {
#ifdef _WIN32
        return lstrlenA(file_name) + 1;
#else
        return static_cast<int>(std::strlen(file_name) + 1);
#endif
    }

    if (output_chars <= 0) {
        return 0;
    }
#ifdef _WIN32
    lstrcpynA(output, file_name, output_chars);
#else
    std::strncpy(output, file_name, static_cast<std::size_t>(output_chars));
    output[output_chars - 1] = '\0';
#endif
    return 0;
}

#ifdef _WIN32
void AfxPostQuitMessage(int exit_code) {
    MfcWinThreadCompat* thread = AfxGetThreadCompat();
    if (thread != nullptr && thread->ole_term_or_free_lib != nullptr) {
        thread->ole_term_or_free_lib(TRUE, TRUE);
    }
    PostQuitMessage(exit_code);
}
#endif

}  // namespace ranker
