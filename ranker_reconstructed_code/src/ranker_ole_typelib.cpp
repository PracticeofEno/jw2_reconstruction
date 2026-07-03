#include "ranker_ole_typelib.h"

#ifdef _WIN32

#include <objbase.h>

namespace ranker {
namespace {

template <typename T>
void release_com(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

template <typename T>
void assign_com(T*& target, T* value) {
    if (value != nullptr) {
        value->AddRef();
    }
    release_com(target);
    target = value;
}

bool same_guid(const GUID& lhs, const GUID& rhs) {
    return IsEqualGUID(lhs, rhs) != FALSE;
}

} // namespace

TypeLibCacheEntry::~TypeLibCacheEntry() {
    release_com(type_info);
    release_com(type_lib);
}

TypeLibCacheEntry& GetTypeLibCacheEntry(TypeLibCache& cache,
    std::uintptr_t module_key) {
    auto found = cache.entries.find(module_key);
    if (found != cache.entries.end()) {
        return *found->second;
    }
    auto entry = std::make_unique<TypeLibCacheEntry>(module_key);
    TypeLibCacheEntry& result = *entry;
    cache.entries.emplace(module_key, std::move(entry));
    return result;
}

LONG AddRefTypeLibCacheEntry(TypeLibCacheEntry& entry) {
    if (entry.lock_count == 0) {
        entry.generation = static_cast<std::uintptr_t>(-1);
    }
    return InterlockedIncrement(&entry.lock_count);
}

bool TryGetCachedTypeLib(TypeLibCacheEntry& entry, std::uintptr_t generation,
    ITypeLib** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (out == nullptr || entry.generation != generation || entry.type_lib == nullptr) {
        return false;
    }
    entry.type_lib->AddRef();
    *out = entry.type_lib;
    return true;
}

void StoreCachedTypeLib(TypeLibCacheEntry& entry, std::uintptr_t generation,
    ITypeLib* type_lib) {
    if (type_lib == nullptr) {
        return;
    }
    entry.generation = generation;
    entry.type_info_guid = GUID{};
    release_com(entry.type_info);
    assign_com(entry.type_lib, type_lib);
}

bool TryGetCachedTypeInfo(TypeLibCacheEntry& entry, std::uintptr_t generation,
    const GUID& guid, ITypeInfo** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (out == nullptr || entry.generation != generation || entry.type_info == nullptr ||
        !same_guid(entry.type_info_guid, guid)) {
        return false;
    }
    entry.type_info->AddRef();
    *out = entry.type_info;
    return true;
}

void StoreCachedTypeInfo(TypeLibCacheEntry& entry, std::uintptr_t generation,
    const GUID& guid, ITypeInfo* type_info) {
    if (type_info == nullptr || entry.generation != generation) {
        return;
    }
    entry.type_info_guid = guid;
    assign_com(entry.type_info, type_info);
}

HRESULT ResolveTypeInfoOfGuid(TypeLibCache& cache, std::uintptr_t module_key,
    std::uintptr_t generation, const GUID& guid, ITypeInfo** out,
    LPCOLESTR type_library_path) {
    if (out == nullptr) {
        return E_POINTER;
    }
    *out = nullptr;

    TypeLibCacheEntry& entry = GetTypeLibCacheEntry(cache, module_key);
    if (TryGetCachedTypeInfo(entry, generation, guid, out)) {
        return S_OK;
    }

    ITypeLib* type_lib = nullptr;
    if (!TryGetCachedTypeLib(entry, generation, &type_lib)) {
        HRESULT hr = type_library_path != nullptr ?
            LoadTypeLib(type_library_path, &type_lib) : TYPE_E_CANTLOADLIBRARY;
        if (FAILED(hr)) {
            return hr;
        }
        StoreCachedTypeLib(entry, generation, type_lib);
    }

    HRESULT hr = type_lib->GetTypeInfoOfGuid(guid, out);
    type_lib->Release();
    if (SUCCEEDED(hr)) {
        StoreCachedTypeInfo(entry, generation, guid, *out);
    }
    return hr;
}

} // namespace ranker

#endif
