#include "ranker_ole_variant.h"

#ifdef _WIN32

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace ranker {
namespace {

constexpr VARTYPE kMfcAnsiBstrSourceType = 0x000e;

HRESULT allocate_bstr_from_ansi(BSTR& out, const char* text, VARTYPE source_type) {
    out = nullptr;
    if (text == nullptr) {
        return S_OK;
    }

    if (source_type == kMfcAnsiBstrSourceType) {
        const UINT bytes = static_cast<UINT>(lstrlenA(text));
        out = SysAllocStringByteLen(text, bytes);
    } else {
        const int chars = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (chars <= 0) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        std::wstring wide(static_cast<std::size_t>(chars), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), chars);
        out = SysAllocString(wide.c_str());
    }
    return out != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT access_array_data(SAFEARRAY* array, void** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    return SafeArrayAccessData(array, out);
}

SAFEARRAY* safe_array_from(const OleSafeArrayState& state) {
    return V_ARRAY(const_cast<VARIANTARG*>(&state.variant));
}

bool is_array_variant(const VARIANTARG& variant) {
    return (V_VT(&variant) & VT_ARRAY) != 0 && (V_VT(&variant) & VT_BYREF) == 0;
}

HRESULT require_safe_array(const OleSafeArrayState& state, SAFEARRAY*& out) {
    out = safe_array_from(state);
    return out != nullptr && is_array_variant(state.variant) ? S_OK : E_INVALIDARG;
}

} // namespace

void InitializeOleVariant(VARIANTARG& variant) {
    VariantInit(&variant);
}

HRESULT ClearOleVariant(VARIANTARG& variant) {
    return VariantClear(&variant);
}

HRESULT CopyOleVariant(VARIANTARG& destination, const VARIANTARG& source) {
    return VariantCopy(&destination, const_cast<VARIANTARG*>(&source));
}

HRESULT ChangeOleVariantType(VARIANTARG& destination, VARTYPE vt,
    const VARIANTARG* source) {
    VARIANTARG* active_source = const_cast<VARIANTARG*>(source != nullptr ? source : &destination);
    if (active_source == &destination && V_VT(&destination) == vt) {
        return S_OK;
    }
    return VariantChangeType(&destination, active_source, 0, vt);
}

bool CompareSafeArrays(SAFEARRAY* lhs, SAFEARRAY* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return lhs == rhs;
    }

    const UINT dims = SafeArrayGetDim(lhs);
    if (dims != SafeArrayGetDim(rhs)) {
        return false;
    }
    if (dims == 0) {
        return true;
    }
    const UINT elem_size = SafeArrayGetElemsize(lhs);
    if (elem_size != SafeArrayGetElemsize(rhs)) {
        return false;
    }

    std::size_t element_count = 1;
    for (UINT dim = 1; dim <= dims; ++dim) {
        LONG lhs_low = 0;
        LONG lhs_high = -1;
        LONG rhs_low = 0;
        LONG rhs_high = -1;
        if (FAILED(SafeArrayGetLBound(lhs, dim, &lhs_low)) ||
            FAILED(SafeArrayGetUBound(lhs, dim, &lhs_high)) ||
            FAILED(SafeArrayGetLBound(rhs, dim, &rhs_low)) ||
            FAILED(SafeArrayGetUBound(rhs, dim, &rhs_high))) {
            return false;
        }
        if ((lhs_high - lhs_low) != (rhs_high - rhs_low)) {
            return false;
        }
        const auto count = static_cast<std::size_t>(lhs_high - lhs_low + 1);
        if (count != 0 &&
            element_count > std::numeric_limits<std::size_t>::max() / count) {
            return false;
        }
        element_count *= count;
    }

    void* lhs_data = nullptr;
    void* rhs_data = nullptr;
    if (FAILED(access_array_data(lhs, &lhs_data))) {
        return false;
    }
    if (FAILED(access_array_data(rhs, &rhs_data))) {
        SafeArrayUnaccessData(lhs);
        return false;
    }
    const std::size_t byte_count = element_count * elem_size;
    const bool equal = byte_count == 0 || std::memcmp(lhs_data, rhs_data, byte_count) == 0;
    SafeArrayUnaccessData(rhs);
    SafeArrayUnaccessData(lhs);
    return equal;
}

bool FinishSafeArrayComparison(SAFEARRAY* lhs, SAFEARRAY* rhs) {
    return CompareSafeArrays(lhs, rhs);
}

bool CompareOleVariantsExact(const VARIANTARG& lhs, const VARIANTARG& rhs) {
    if (&lhs == &rhs) {
        return true;
    }
    if (V_VT(&lhs) != V_VT(&rhs)) {
        return false;
    }

    switch (V_VT(&lhs)) {
    case VT_EMPTY:
    case VT_NULL:
        return true;
    case VT_I2:
        return V_I2(&lhs) == V_I2(&rhs);
    case VT_I4:
    case VT_ERROR:
        return V_I4(&lhs) == V_I4(&rhs);
    case VT_R4:
        return V_R4(&lhs) == V_R4(&rhs);
    case VT_R8:
    case VT_DATE:
        return V_R8(&lhs) == V_R8(&rhs);
    case VT_CY:
        return V_CY(&lhs).int64 == V_CY(&rhs).int64;
    case VT_BSTR: {
        const UINT lhs_bytes = SysStringByteLen(V_BSTR(&lhs));
        const UINT rhs_bytes = SysStringByteLen(V_BSTR(&rhs));
        return lhs_bytes == rhs_bytes &&
            (lhs_bytes == 0 || std::memcmp(V_BSTR(&lhs), V_BSTR(&rhs), lhs_bytes) == 0);
    }
    case VT_UNKNOWN:
    case VT_DISPATCH:
        return V_UNKNOWN(&lhs) == V_UNKNOWN(&rhs);
    case VT_BOOL:
        return V_BOOL(&lhs) == V_BOOL(&rhs);
    case VT_UI1:
        return V_UI1(&lhs) == V_UI1(&rhs);
    default:
        if ((V_VT(&lhs) & VT_ARRAY) != 0 && (V_VT(&lhs) & VT_BYREF) == 0) {
            return CompareSafeArrays(V_ARRAY(&lhs), V_ARRAY(&rhs));
        }
        return false;
    }
}

HRESULT EnsureOleVariantByteArray(VARIANTARG& variant, ULONG byte_count) {
    if (V_VT(&variant) == (VT_ARRAY | VT_UI1) && V_ARRAY(&variant) != nullptr &&
        SafeArrayGetDim(V_ARRAY(&variant)) == 1) {
        LONG low = 0;
        LONG high = -1;
        HRESULT hr = SafeArrayGetLBound(V_ARRAY(&variant), 1, &low);
        if (FAILED(hr)) {
            return hr;
        }
        hr = SafeArrayGetUBound(V_ARRAY(&variant), 1, &high);
        if (FAILED(hr)) {
            return hr;
        }
        const ULONG current = high >= low ? static_cast<ULONG>(high - low + 1) : 0;
        if (current == byte_count) {
            return S_OK;
        }
        SAFEARRAYBOUND bound{};
        bound.cElements = byte_count;
        bound.lLbound = low;
        return SafeArrayRedim(V_ARRAY(&variant), &bound);
    }

    HRESULT hr = VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    SAFEARRAYBOUND bound{};
    bound.cElements = byte_count;
    bound.lLbound = 0;
    V_VT(&variant) = VT_ARRAY | VT_UI1;
    V_ARRAY(&variant) = SafeArrayCreate(VT_UI1, 1, &bound);
    return V_ARRAY(&variant) != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT CopyBinaryDataToSafeArray(SAFEARRAY* array, const void* data,
    ULONG byte_count) {
    if (byte_count != 0 && data == nullptr) {
        return E_POINTER;
    }
    void* out = nullptr;
    HRESULT hr = SafeArrayAccessData(array, &out);
    if (FAILED(hr)) {
        return hr;
    }
    if (byte_count != 0) {
        std::memcpy(out, data, byte_count);
    }
    return SafeArrayUnaccessData(array);
}

HRESULT _AfxCopyBinaryData(SAFEARRAY* array, const void* data,
    ULONG byte_count) {
    return CopyBinaryDataToSafeArray(array, data, byte_count);
}

HRESULT AssignOleVariantFromBinary(VARIANTARG& variant, const void* data,
    ULONG byte_count) {
    HRESULT hr = EnsureOleVariantByteArray(variant, byte_count);
    if (FAILED(hr)) {
        return hr;
    }
    return CopyBinaryDataToSafeArray(V_ARRAY(&variant), data, byte_count);
}

HRESULT AssignOleVariantFromGlobalMemory(VARIANTARG& variant, HGLOBAL memory,
    ULONG byte_count) {
    void* data = GlobalLock(memory);
    if (data == nullptr && byte_count != 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT hr = AssignOleVariantFromBinary(variant, data, byte_count);
    GlobalUnlock(memory);
    return hr;
}

HRESULT ConstructOleVariantFromAnsiString(VARIANTARG& variant, const char* text,
    VARTYPE source_type) {
    VariantInit(&variant);
    V_VT(&variant) = VT_BSTR;
    return allocate_bstr_from_ansi(V_BSTR(&variant), text, source_type);
}

HRESULT AssignOleVariantFromAnsiString(VARIANTARG& variant, const char* text,
    VARTYPE source_type) {
    HRESULT hr = VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    return ConstructOleVariantFromAnsiString(variant, text, source_type);
}

HRESULT AssignOleVariantByte(VARIANTARG& variant, BYTE value) {
    HRESULT hr = V_VT(&variant) == VT_UI1 ? S_OK : VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = VT_UI1;
    V_UI1(&variant) = value;
    return S_OK;
}

HRESULT AssignOleVariantI2OrBool(VARIANTARG& variant, SHORT value, VARTYPE vt) {
    if (vt != VT_I2 && vt != VT_BOOL) {
        return E_INVALIDARG;
    }
    HRESULT hr = VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = vt;
    if (vt == VT_BOOL) {
        V_BOOL(&variant) = value == 0 ? VARIANT_FALSE : VARIANT_TRUE;
    } else {
        V_I2(&variant) = value;
    }
    return S_OK;
}

HRESULT AssignOleVariantI4ErrorOrBool(VARIANTARG& variant, LONG value, VARTYPE vt) {
    if (vt != VT_I4 && vt != VT_ERROR && vt != VT_BOOL) {
        return E_INVALIDARG;
    }
    HRESULT hr = VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = vt;
    if (vt == VT_BOOL) {
        V_BOOL(&variant) = value == 0 ? VARIANT_FALSE : VARIANT_TRUE;
    } else if (vt == VT_ERROR) {
        V_ERROR(&variant) = value;
    } else {
        V_I4(&variant) = value;
    }
    return S_OK;
}

HRESULT AssignOleVariantCurrency(VARIANTARG& variant, CY value) {
    HRESULT hr = V_VT(&variant) == VT_CY ? S_OK : VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = VT_CY;
    V_CY(&variant) = value;
    return S_OK;
}

HRESULT AssignOleVariantR4(VARIANTARG& variant, FLOAT value) {
    HRESULT hr = V_VT(&variant) == VT_R4 ? S_OK : VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = VT_R4;
    V_R4(&variant) = value;
    return S_OK;
}

HRESULT AssignOleVariantR8(VARIANTARG& variant, DOUBLE value) {
    HRESULT hr = V_VT(&variant) == VT_R8 ? S_OK : VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = VT_R8;
    V_R8(&variant) = value;
    return S_OK;
}

HRESULT AssignOleVariantDate(VARIANTARG& variant, DATE value) {
    HRESULT hr = V_VT(&variant) == VT_DATE ? S_OK : VariantClear(&variant);
    if (FAILED(hr)) {
        return hr;
    }
    V_VT(&variant) = VT_DATE;
    V_DATE(&variant) = value;
    return S_OK;
}

u32 HashOleVariantValue(const VARIANTARG& variant) {
    switch (V_VT(&variant)) {
    case VT_EMPTY:
    case VT_NULL:
        return 0;
    case VT_I2:
        return static_cast<u32>(V_I2(&variant));
    case VT_I4:
    case VT_ERROR:
        return static_cast<u32>(V_I4(&variant));
    case VT_UI1:
        return V_UI1(&variant);
    case VT_BOOL:
        return static_cast<u32>(V_BOOL(&variant));
    case VT_BSTR:
        return SysStringByteLen(V_BSTR(&variant));
    default:
        return static_cast<u32>(V_VT(&variant));
    }
}

void InitializeOleSafeArray(OleSafeArrayState& state) {
    std::memset(&state, 0, sizeof(state));
    VariantInit(&state.variant);
}

HRESULT RefreshOleSafeArrayMetadata(OleSafeArrayState& state) {
    SAFEARRAY* array = safe_array_from(state);
    if (array == nullptr || !is_array_variant(state.variant)) {
        state.dimensions = 0;
        state.element_size = 0;
        return S_OK;
    }
    state.dimensions = SafeArrayGetDim(array);
    state.element_size = SafeArrayGetElemsize(array);
    return S_OK;
}

HRESULT ConstructOleSafeArrayFromArray(OleSafeArrayState& state, SAFEARRAY* source,
    VARTYPE vt) {
    InitializeOleSafeArray(state);
    V_VT(&state.variant) = vt | VT_ARRAY;
    HRESULT hr = SafeArrayCopy(source, &V_ARRAY(&state.variant));
    if (FAILED(hr)) {
        VariantInit(&state.variant);
        return hr;
    }
    return RefreshOleSafeArrayMetadata(state);
}

HRESULT CopyOleSafeArrayVariant(OleSafeArrayState& state, const VARIANTARG& source) {
    if (!is_array_variant(source)) {
        return E_INVALIDARG;
    }
    HRESULT hr = VariantClear(&state.variant);
    if (FAILED(hr)) {
        return hr;
    }
    hr = VariantCopy(&state.variant, const_cast<VARIANTARG*>(&source));
    if (FAILED(hr)) {
        VariantInit(&state.variant);
        return hr;
    }
    return RefreshOleSafeArrayMetadata(state);
}

HRESULT CopyOleSafeArrayVariantRef(OleSafeArrayState& state,
    const VARIANTARG& source) {
    return CopyOleSafeArrayVariant(state, source);
}

HRESULT CopyOleSafeArrayVariantPtr(OleSafeArrayState& state,
    const VARIANTARG* source) {
    return source == nullptr ? E_POINTER : CopyOleSafeArrayVariant(state, *source);
}

HRESULT CopyOleSafeArrayOleVariantRef(OleSafeArrayState& state,
    const VARIANTARG& source) {
    return CopyOleSafeArrayVariant(state, source);
}

HRESULT CopyOleSafeArrayOleVariantPtr(OleSafeArrayState& state,
    const VARIANTARG* source) {
    return source == nullptr ? E_POINTER : CopyOleSafeArrayVariant(state, *source);
}

HRESULT AttachOleSafeArrayVariant(OleSafeArrayState& state, VARIANTARG& source) {
    if (!is_array_variant(source)) {
        return E_INVALIDARG;
    }
    HRESULT hr = VariantClear(&state.variant);
    if (FAILED(hr)) {
        return hr;
    }
    std::memcpy(&state.variant, &source, sizeof(VARIANTARG));
    V_VT(&source) = VT_EMPTY;
    return RefreshOleSafeArrayMetadata(state);
}

VARIANTARG DetachOleSafeArrayVariant(OleSafeArrayState& state) {
    VARIANTARG detached{};
    std::memcpy(&detached, &state.variant, sizeof(VARIANTARG));
    VariantInit(&state.variant);
    state.dimensions = 0;
    state.element_size = 0;
    return detached;
}

bool CompareOleSafeArraysByVariant(const VARIANTARG& lhs, const VARIANTARG& rhs) {
    return V_VT(&lhs) == V_VT(&rhs) && is_array_variant(lhs) &&
        CompareSafeArrays(V_ARRAY(const_cast<VARIANTARG*>(&lhs)),
            V_ARRAY(const_cast<VARIANTARG*>(&rhs)));
}

HRESULT CreateOleSafeArray(OleSafeArrayState& state, VARTYPE vt, UINT dimensions,
    const SAFEARRAYBOUND* bounds) {
    if (dimensions == 0 || bounds == nullptr ||
        (vt & (VT_ARRAY | VT_BYREF | VT_VECTOR)) != 0 || vt == VT_EMPTY ||
        vt == VT_NULL) {
        return E_INVALIDARG;
    }

    HRESULT hr = VariantClear(&state.variant);
    if (FAILED(hr)) {
        return hr;
    }

    SAFEARRAY* array = SafeArrayCreate(vt, dimensions,
        const_cast<SAFEARRAYBOUND*>(bounds));
    if (array == nullptr) {
        VariantInit(&state.variant);
        return E_OUTOFMEMORY;
    }

    V_VT(&state.variant) = vt | VT_ARRAY;
    V_ARRAY(&state.variant) = array;
    state.dimensions = dimensions;
    state.element_size = SafeArrayGetElemsize(array);
    return S_OK;
}

HRESULT CreateOneDimOleSafeArray(OleSafeArrayState& state, VARTYPE vt,
    ULONG element_count, LONG lower_bound, const void* initial_data) {
    if (element_count == 0) {
        return E_INVALIDARG;
    }

    SAFEARRAYBOUND bound{};
    bound.cElements = element_count;
    bound.lLbound = lower_bound;
    HRESULT hr = CreateOleSafeArray(state, vt, 1, &bound);
    if (FAILED(hr) || initial_data == nullptr) {
        return hr;
    }

    void* destination = nullptr;
    hr = AccessOleSafeArrayData(state, &destination);
    if (FAILED(hr)) {
        return hr;
    }
    const std::size_t byte_count =
        static_cast<std::size_t>(state.element_size) * element_count;
    if (byte_count != 0) {
        std::memcpy(destination, initial_data, byte_count);
    }
    return UnaccessOleSafeArrayData(state);
}

HRESULT CreateMultiDimOleSafeArray(OleSafeArrayState& state, VARTYPE vt,
    const ULONG* element_counts, UINT dimensions) {
    if (element_counts == nullptr || dimensions == 0) {
        return E_INVALIDARG;
    }
    std::vector<SAFEARRAYBOUND> bounds(dimensions);
    for (UINT index = 0; index < dimensions; ++index) {
        bounds[index].cElements = element_counts[index];
        bounds[index].lLbound = 0;
    }
    return CreateOleSafeArray(state, vt, dimensions, bounds.data());
}

HRESULT GetOneDimOleSafeArraySize(const OleSafeArrayState& state, ULONG& out_count) {
    out_count = 0;
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    if (FAILED(hr)) {
        return hr;
    }
    if (SafeArrayGetDim(array) != 1) {
        return E_INVALIDARG;
    }

    LONG lower = 0;
    LONG upper = -1;
    hr = SafeArrayGetLBound(array, 1, &lower);
    if (FAILED(hr)) {
        return hr;
    }
    hr = SafeArrayGetUBound(array, 1, &upper);
    if (FAILED(hr)) {
        return hr;
    }
    out_count = upper >= lower ? static_cast<ULONG>(upper - lower + 1) : 0;
    return S_OK;
}

HRESULT RedimOneDimOleSafeArray(OleSafeArrayState& state, ULONG element_count) {
    SAFEARRAYBOUND bound{};
    bound.cElements = element_count;
    bound.lLbound = 0;
    return RedimOleSafeArray(state, bound);
}

HRESULT AccessOleSafeArrayData(OleSafeArrayState& state, void** out) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    if (FAILED(hr)) {
        if (out != nullptr) {
            *out = nullptr;
        }
        return hr;
    }
    return SafeArrayAccessData(array, out);
}

HRESULT UnaccessOleSafeArrayData(OleSafeArrayState& state) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayUnaccessData(array);
}

HRESULT AllocOleSafeArrayData(OleSafeArrayState& state) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayAllocData(array);
}

HRESULT AllocOleSafeArrayDescriptor(OleSafeArrayState& state, UINT dimensions) {
    SAFEARRAY* descriptor = nullptr;
    HRESULT hr = SafeArrayAllocDescriptor(dimensions, &descriptor);
    if (FAILED(hr)) {
        return hr;
    }
    HRESULT clear_hr = VariantClear(&state.variant);
    if (FAILED(clear_hr)) {
        SafeArrayDestroyDescriptor(descriptor);
        return clear_hr;
    }
    V_ARRAY(&state.variant) = descriptor;
    state.dimensions = dimensions;
    state.element_size = 0;
    return S_OK;
}

HRESULT CopyOleSafeArray(const OleSafeArrayState& state, SAFEARRAY** out) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayCopy(array, out);
}

HRESULT GetOleSafeArrayLBound(const OleSafeArrayState& state, UINT dimension,
    LONG& out) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayGetLBound(array, dimension, &out);
}

HRESULT GetOleSafeArrayUBound(const OleSafeArrayState& state, UINT dimension,
    LONG& out) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayGetUBound(array, dimension, &out);
}

HRESULT GetOleSafeArrayElement(const OleSafeArrayState& state, LONG* indices,
    void* out) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayGetElement(array, indices, out);
}

HRESULT PtrOfOleSafeArrayIndex(const OleSafeArrayState& state, LONG* indices,
    void** out) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayPtrOfIndex(array, indices, out);
}

HRESULT PutOleSafeArrayElement(OleSafeArrayState& state, LONG* indices, void* value) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayPutElement(array, indices, value);
}

HRESULT RedimOleSafeArray(OleSafeArrayState& state, SAFEARRAYBOUND& bound) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    if (FAILED(hr)) {
        return hr;
    }
    hr = SafeArrayRedim(array, &bound);
    if (SUCCEEDED(hr)) {
        RefreshOleSafeArrayMetadata(state);
    }
    return hr;
}

HRESULT LockOleSafeArray(OleSafeArrayState& state) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayLock(array);
}

HRESULT UnlockOleSafeArray(OleSafeArrayState& state) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayUnlock(array);
}

HRESULT DestroyOleSafeArray(OleSafeArrayState& state) {
    SAFEARRAY* array = safe_array_from(state);
    if (array == nullptr) {
        VariantInit(&state.variant);
        state.dimensions = 0;
        state.element_size = 0;
        return S_OK;
    }
    HRESULT hr = SafeArrayDestroy(array);
    if (SUCCEEDED(hr)) {
        VariantInit(&state.variant);
        state.dimensions = 0;
        state.element_size = 0;
    }
    return hr;
}

HRESULT DestroyOleSafeArrayData(OleSafeArrayState& state) {
    SAFEARRAY* array = nullptr;
    HRESULT hr = require_safe_array(state, array);
    return FAILED(hr) ? hr : SafeArrayDestroyData(array);
}

HRESULT DestroyOleSafeArrayDescriptor(OleSafeArrayState& state) {
    SAFEARRAY* array = safe_array_from(state);
    if (array == nullptr) {
        return S_OK;
    }
    HRESULT hr = SafeArrayDestroyDescriptor(array);
    if (SUCCEEDED(hr)) {
        VariantInit(&state.variant);
        state.dimensions = 0;
        state.element_size = 0;
    }
    return hr;
}

} // namespace ranker

#endif
