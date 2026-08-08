#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"

#include <typeinfo>

namespace ranker {

namespace {

MfcRuntimeClassCompat g_cobject_runtime_class{
    "CObject", static_cast<int>(sizeof(MfcObjectCompat)), 0xffff, nullptr,
    nullptr, nullptr};
MfcRuntimeClassCompat* g_first_runtime_class = &g_cobject_runtime_class;

} // namespace

MfcRuntimeClassCompat* AddRuntimeClass(MfcRuntimeClassCompat* runtime_class);

MfcRuntimeClassCompat* GetCObjectRuntimeClass() {
    return &g_cobject_runtime_class;
}

MfcObjectCompat& ConstructCObject(MfcObjectCompat& object) {
    object.runtime_class = GetCObjectRuntimeClass();
    return object;
}

void DestroyCObject(MfcObjectCompat& object) {
    object.runtime_class = GetCObjectRuntimeClass();
}

MfcObjectCompat* DeleteCObjectScalarDtor(MfcObjectCompat* object,
    unsigned flags) {
    if (object == nullptr) {
        return nullptr;
    }
    DestroyCObject(*object);
    if ((flags & 1U) != 0U) {
        MfcDebugDeleteClientBlock(object);
    }
    return object;
}

void CObjectSerializeNoop() {
}

void* CObjectSerializeReturnArchive(void* object, void* archive) {
    (void)object;
    return archive;
}

bool RuntimeClassIsDerivedFrom(const MfcRuntimeClassCompat* candidate,
    const MfcRuntimeClassCompat* target) {
    for (const MfcRuntimeClassCompat* current = candidate; current != nullptr;
         current = current->base_class) {
        if (current == target) {
            return true;
        }
    }
    return false;
}

bool ObjectIsKindOfRuntimeClass(const MfcObjectCompat* object,
    const MfcRuntimeClassCompat* target) {
    if (object == nullptr || object->runtime_class == nullptr ||
        target == nullptr) {
        return false;
    }
    return RuntimeClassIsDerivedFrom(object->runtime_class, target);
}

MfcObjectCompat* AfxDynamicDownCast(const MfcRuntimeClassCompat* target,
    MfcObjectCompat* object) {
    return ObjectIsKindOfRuntimeClass(object, target) ? object : nullptr;
}

MfcObjectCompat* AfxStaticDownCast(const MfcRuntimeClassCompat* target,
    MfcObjectCompat* object) {
    if (object != nullptr && !ObjectIsKindOfRuntimeClass(object, target)) {
        throw std::bad_cast();
    }
    return object;
}

void AfxAssertValidObject(const MfcObjectCompat* object,
    const char* file, int line) {
    if (object == nullptr || object->runtime_class == nullptr) {
        CrtDbgReport(2, file, line, nullptr, "invalid MFC object");
    }
}

void CObjectAssertValid(const MfcObjectCompat* object) {
    AfxAssertValidObject(object, "objcore.cpp", 0x70);
}

void AssertValid(const MfcObjectCompat* object) {
    CObjectAssertValid(object);
}

void* RuntimeClassCreateObject(MfcRuntimeClassCompat& runtime_class) {
    if (runtime_class.create_object == nullptr) {
        AfxTraceOutput("Error: Trying to create object which is not "
            "DECLARE_DYNCREATE or DECLARE_SERIAL: %s.\n",
            runtime_class.class_name);
        return nullptr;
    }
    return runtime_class.create_object();
}

void* CreateObject(MfcRuntimeClassCompat* runtime_class) {
    return runtime_class == nullptr ? nullptr
        : RuntimeClassCreateObject(*runtime_class);
}



POINT CPoint() {
    return POINT{};
}

POINT CPoint(LONG x, LONG y) {
    return POINT{x, y};
}

void RemoveAll(MfcPtrListCompat& list) {
    PtrListRemoveAll(list);
}

void* RuntimeClassCreateObjectEpilogue(void* object) {
    return object;
}

MfcRuntimeClassCompat* AfxClassInitObject(MfcRuntimeClassCompat* runtime_class) {
    return AddRuntimeClass(runtime_class);
}

MfcRuntimeClassCompat* AddRuntimeClass(MfcRuntimeClassCompat* runtime_class) {
    if (runtime_class == nullptr) {
        return nullptr;
    }
    runtime_class->next_class = g_first_runtime_class;
    g_first_runtime_class = runtime_class;
    return runtime_class;
}

MfcRuntimeClassCompat* GetFirstRuntimeClass() {
    return g_first_runtime_class;
}

} // namespace ranker
