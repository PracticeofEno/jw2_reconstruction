#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#endif

#include <cstddef>

namespace ranker {

#ifdef _WIN32
struct OleSafeArrayState {
    VARIANTARG variant{};
    ULONG element_size = 0;
    ULONG dimensions = 0;
};

void InitializeOleVariant(VARIANTARG& variant);
HRESULT ClearOleVariant(VARIANTARG& variant);
HRESULT CopyOleVariant(VARIANTARG& destination, const VARIANTARG& source);
HRESULT ChangeOleVariantType(VARIANTARG& destination, VARTYPE vt,
    const VARIANTARG* source = nullptr);
bool CompareSafeArrays(SAFEARRAY* lhs, SAFEARRAY* rhs);
bool FinishSafeArrayComparison(SAFEARRAY* lhs, SAFEARRAY* rhs);
bool CompareOleVariantsExact(const VARIANTARG& lhs, const VARIANTARG& rhs);

HRESULT EnsureOleVariantByteArray(VARIANTARG& variant, ULONG byte_count);
HRESULT CopyBinaryDataToSafeArray(SAFEARRAY* array, const void* data,
    ULONG byte_count);
HRESULT _AfxCopyBinaryData(SAFEARRAY* array, const void* data,
    ULONG byte_count);
HRESULT AssignOleVariantFromBinary(VARIANTARG& variant, const void* data,
    ULONG byte_count);
HRESULT AssignOleVariantFromGlobalMemory(VARIANTARG& variant, HGLOBAL memory,
    ULONG byte_count);

HRESULT ConstructOleVariantFromAnsiString(VARIANTARG& variant, const char* text,
    VARTYPE source_type = VT_BSTR);
HRESULT AssignOleVariantFromAnsiString(VARIANTARG& variant, const char* text,
    VARTYPE source_type = VT_BSTR);
HRESULT AssignOleVariantByte(VARIANTARG& variant, BYTE value);
HRESULT AssignOleVariantI2OrBool(VARIANTARG& variant, SHORT value, VARTYPE vt);
HRESULT AssignOleVariantI4ErrorOrBool(VARIANTARG& variant, LONG value, VARTYPE vt);
HRESULT AssignOleVariantCurrency(VARIANTARG& variant, CY value);
HRESULT AssignOleVariantR4(VARIANTARG& variant, FLOAT value);
HRESULT AssignOleVariantR8(VARIANTARG& variant, DOUBLE value);
HRESULT AssignOleVariantDate(VARIANTARG& variant, DATE value);
u32 HashOleVariantValue(const VARIANTARG& variant);

void InitializeOleSafeArray(OleSafeArrayState& state);
HRESULT RefreshOleSafeArrayMetadata(OleSafeArrayState& state);
HRESULT ConstructOleSafeArrayFromArray(OleSafeArrayState& state, SAFEARRAY* source,
    VARTYPE vt);
HRESULT CopyOleSafeArrayVariant(OleSafeArrayState& state, const VARIANTARG& source);
HRESULT CopyOleSafeArrayVariantRef(OleSafeArrayState& state,
    const VARIANTARG& source);
HRESULT CopyOleSafeArrayVariantPtr(OleSafeArrayState& state,
    const VARIANTARG* source);
HRESULT CopyOleSafeArrayOleVariantRef(OleSafeArrayState& state,
    const VARIANTARG& source);
HRESULT CopyOleSafeArrayOleVariantPtr(OleSafeArrayState& state,
    const VARIANTARG* source);
HRESULT AttachOleSafeArrayVariant(OleSafeArrayState& state, VARIANTARG& source);
VARIANTARG DetachOleSafeArrayVariant(OleSafeArrayState& state);
bool CompareOleSafeArraysByVariant(const VARIANTARG& lhs, const VARIANTARG& rhs);
HRESULT CreateOleSafeArray(OleSafeArrayState& state, VARTYPE vt, UINT dimensions,
    const SAFEARRAYBOUND* bounds);
HRESULT CreateOneDimOleSafeArray(OleSafeArrayState& state, VARTYPE vt,
    ULONG element_count, LONG lower_bound = 0, const void* initial_data = nullptr);
HRESULT CreateMultiDimOleSafeArray(OleSafeArrayState& state, VARTYPE vt,
    const ULONG* element_counts, UINT dimensions);
HRESULT GetOneDimOleSafeArraySize(const OleSafeArrayState& state, ULONG& out_count);
HRESULT RedimOneDimOleSafeArray(OleSafeArrayState& state, ULONG element_count);
HRESULT AccessOleSafeArrayData(OleSafeArrayState& state, void** out);
HRESULT UnaccessOleSafeArrayData(OleSafeArrayState& state);
HRESULT AllocOleSafeArrayData(OleSafeArrayState& state);
HRESULT AllocOleSafeArrayDescriptor(OleSafeArrayState& state, UINT dimensions);
HRESULT CopyOleSafeArray(const OleSafeArrayState& state, SAFEARRAY** out);
HRESULT GetOleSafeArrayLBound(const OleSafeArrayState& state, UINT dimension,
    LONG& out);
HRESULT GetOleSafeArrayUBound(const OleSafeArrayState& state, UINT dimension,
    LONG& out);
HRESULT GetOleSafeArrayElement(const OleSafeArrayState& state, LONG* indices,
    void* out);
HRESULT PtrOfOleSafeArrayIndex(const OleSafeArrayState& state, LONG* indices,
    void** out);
HRESULT PutOleSafeArrayElement(OleSafeArrayState& state, LONG* indices, void* value);
HRESULT RedimOleSafeArray(OleSafeArrayState& state, SAFEARRAYBOUND& bound);
HRESULT LockOleSafeArray(OleSafeArrayState& state);
HRESULT UnlockOleSafeArray(OleSafeArrayState& state);
HRESULT DestroyOleSafeArray(OleSafeArrayState& state);
HRESULT DestroyOleSafeArrayData(OleSafeArrayState& state);
HRESULT DestroyOleSafeArrayDescriptor(OleSafeArrayState& state);
#endif

} // namespace ranker
