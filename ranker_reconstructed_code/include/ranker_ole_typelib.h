#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#endif

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace ranker {

#ifdef _WIN32
struct TypeLibCacheEntry {
    std::uintptr_t module_key = 0;
    std::uintptr_t generation = static_cast<std::uintptr_t>(-1);
    ITypeLib* type_lib = nullptr;
    GUID type_info_guid{};
    ITypeInfo* type_info = nullptr;
    volatile LONG lock_count = 0;

    TypeLibCacheEntry() = default;
    explicit TypeLibCacheEntry(std::uintptr_t key) : module_key(key) {}
    TypeLibCacheEntry(const TypeLibCacheEntry&) = delete;
    TypeLibCacheEntry& operator=(const TypeLibCacheEntry&) = delete;
    ~TypeLibCacheEntry();
};

struct TypeLibCache {
    std::unordered_map<std::uintptr_t, std::unique_ptr<TypeLibCacheEntry>> entries;
};

TypeLibCacheEntry& GetTypeLibCacheEntry(TypeLibCache& cache, std::uintptr_t module_key);
LONG AddRefTypeLibCacheEntry(TypeLibCacheEntry& entry);
bool TryGetCachedTypeLib(TypeLibCacheEntry& entry, std::uintptr_t generation,
    ITypeLib** out);
void StoreCachedTypeLib(TypeLibCacheEntry& entry, std::uintptr_t generation,
    ITypeLib* type_lib);
bool TryGetCachedTypeInfo(TypeLibCacheEntry& entry, std::uintptr_t generation,
    const GUID& guid, ITypeInfo** out);
void StoreCachedTypeInfo(TypeLibCacheEntry& entry, std::uintptr_t generation,
    const GUID& guid, ITypeInfo* type_info);
HRESULT ResolveTypeInfoOfGuid(TypeLibCache& cache, std::uintptr_t module_key,
    std::uintptr_t generation, const GUID& guid, ITypeInfo** out,
    LPCOLESTR type_library_path = nullptr);
#endif

} // namespace ranker
