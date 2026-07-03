#include "ranker_ole_image_data.h"

#ifdef _WIN32

#include <cstring>
#include <new>
#include <stdexcept>

namespace ranker {
namespace {

constexpr DWORD kStorageCreateFlags = STGM_CREATE | STGM_READWRITE |
    STGM_SHARE_EXCLUSIVE;

void release_if_present(IUnknown* object) {
    if (object != nullptr) {
        object->Release();
    }
}

HRESULT not_implemented() {
    return E_NOTIMPL;
}

[[noreturn]] void throw_ole_failure(HRESULT result) {
    (void)result;
    throw std::runtime_error("OLE HRESULT failure");
}

void throw_if_failed(HRESULT result) {
    if (FAILED(result)) {
        throw_ole_failure(result);
    }
}

} // namespace

OleBitmapDataObject::OleBitmapDataObject() {
    InitializeOleBitmapDataObject(this);
}

OleBitmapDataObject::~OleBitmapDataObject() {
    HandleOleBitmapDataObjectDestructor(this);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }

    if (CompareGuidBytes(&iid, &IID_IUnknown) ||
        CompareGuidBytes(&iid, &IID_IDataObject)) {
        *object = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE OleBitmapDataObject::AddRef() {
    ++ref_count_;
    return ref_count_;
}

ULONG STDMETHODCALLTYPE OleBitmapDataObject::Release() {
    --ref_count_;
    const ULONG remaining = ref_count_;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::GetData(FORMATETC*, STGMEDIUM* medium) {
    if (medium == nullptr) {
        return E_POINTER;
    }

    HANDLE duplicate = OleDuplicateData(medium_.hBitmap, CF_BITMAP, 0);
    if (duplicate == nullptr) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    }

    medium->tymed = TYMED_GDI;
    medium->hBitmap = static_cast<HBITMAP>(duplicate);
    medium->pUnkForRelease = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::GetDataHere(FORMATETC*, STGMEDIUM*) {
    return not_implemented();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::QueryGetData(FORMATETC*) {
    return not_implemented();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::GetCanonicalFormatEtc(FORMATETC*,
    FORMATETC*) {
    return not_implemented();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::SetData(FORMATETC* format,
    STGMEDIUM* medium, BOOL) {
    if (format == nullptr || medium == nullptr) {
        return E_POINTER;
    }

    format_ = *format;
    medium_ = *medium;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::EnumFormatEtc(DWORD,
    IEnumFORMATETC**) {
    return not_implemented();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::DAdvise(FORMATETC*, DWORD,
    IAdviseSink*, DWORD*) {
    return not_implemented();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::DUnadvise(DWORD) {
    return not_implemented();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject::EnumDAdvise(IEnumSTATDATA**) {
    return not_implemented();
}

const FORMATETC& OleBitmapDataObject::format() const {
    return format_;
}

const STGMEDIUM& OleBitmapDataObject::medium() const {
    return medium_;
}

bool OleBitmapDataObject::releases_stored_medium() const {
    return release_stored_medium_;
}

void OleBitmapDataObject::set_releases_stored_medium(bool release) {
    release_stored_medium_ = release;
}

OleBitmapDataObject* InitializeOleBitmapDataObject(OleBitmapDataObject* object) {
    if (object == nullptr) {
        return nullptr;
    }

    InitializeOleBitmapDataObjectBase(object);
    object->set_releases_stored_medium(false);
    return object;
}

OleBitmapDataObject* InitializeOleBitmapDataObjectBase(OleBitmapDataObject* object) {
    return InitializeOleBitmapDataObjectNoopBase(object);
}

OleBitmapDataObject* InitializeOleBitmapDataObjectNoopBase(OleBitmapDataObject* object) {
    return object;
}

void HandleOleBitmapDataObjectDestructor(OleBitmapDataObject* object) {
    if (object != nullptr && object->releases_stored_medium()) {
        STGMEDIUM medium = object->medium();
        ReleaseStgMedium(&medium);
        object->set_releases_stored_medium(false);
    }
}

OleBitmapDataObject* DeleteOleBitmapDataObject(OleBitmapDataObject* object,
    u32 delete_flag) {
    if (object != nullptr) {
        HandleOleBitmapDataObjectDestructor(object);
        if ((delete_flag & 1U) != 0) {
            ::operator delete(object);
        }
    }
    return object;
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_QueryInterface(
    OleBitmapDataObject* object, REFIID iid, void** out_object) {
    return object->QueryInterface(iid, out_object);
}

ULONG STDMETHODCALLTYPE OleBitmapDataObject_AddRef(OleBitmapDataObject* object) {
    return object->AddRef();
}

ULONG STDMETHODCALLTYPE OleBitmapDataObject_Release(OleBitmapDataObject* object) {
    return object->Release();
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_GetData(
    OleBitmapDataObject* object, FORMATETC* format, STGMEDIUM* medium) {
    return object->GetData(format, medium);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_GetDataHere(
    OleBitmapDataObject* object, FORMATETC* format, STGMEDIUM* medium) {
    return object->GetDataHere(format, medium);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_QueryGetData(
    OleBitmapDataObject* object, FORMATETC* format) {
    return object->QueryGetData(format);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_GetCanonicalFormatEtc(
    OleBitmapDataObject* object, FORMATETC* format,
    FORMATETC* canonical_format) {
    return object->GetCanonicalFormatEtc(format, canonical_format);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_SetData(
    OleBitmapDataObject* object, FORMATETC* format, STGMEDIUM* medium,
    BOOL release) {
    return object->SetData(format, medium, release);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_EnumFormatEtc(
    OleBitmapDataObject* object, DWORD direction, IEnumFORMATETC** enumerator) {
    return object->EnumFormatEtc(direction, enumerator);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_DAdvise(
    OleBitmapDataObject* object, FORMATETC* format, DWORD flags,
    IAdviseSink* sink, DWORD* connection) {
    return object->DAdvise(format, flags, sink, connection);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_DUnadvise(
    OleBitmapDataObject* object, DWORD connection) {
    return object->DUnadvise(connection);
}

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_EnumDAdvise(
    OleBitmapDataObject* object, IEnumSTATDATA** enumerator) {
    return object->EnumDAdvise(enumerator);
}

int CompareGuidBytes(const void* left, const void* right) {
    if (left == nullptr || right == nullptr) {
        return 0;
    }
    return std::memcmp(left, right, sizeof(GUID)) == 0 ? 1 : 0;
}

void SetOleBitmapDataObjectBitmap(OleBitmapDataObject& object, HBITMAP bitmap) {
    FORMATETC format{};
    format.cfFormat = CF_BITMAP;
    format.ptd = nullptr;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_GDI;

    STGMEDIUM medium{};
    medium.tymed = TYMED_GDI;
    medium.hBitmap = bitmap;
    medium.pUnkForRelease = nullptr;

    object.SetData(&format, &medium, TRUE);
}

IOleObject* CreateStaticOleObjectFromBitmapData(OleBitmapDataObject& data_object,
    IOleClientSite* client_site, IStorage* storage) {
    IOleObject* ole_object = nullptr;
    const HRESULT result = OleCreateStaticFromData(&data_object, IID_IOleObject,
        OLERENDER_FORMAT, const_cast<FORMATETC*>(&data_object.format()),
        client_site, storage, reinterpret_cast<void**>(&ole_object));
    if (FAILED(result)) {
        throw_ole_failure(result);
    }
    return ole_object;
}

void InsertBitmapAsRichEditOleObject(IRichEditOle* rich_ole, HBITMAP bitmap,
    DWORD user_data) {
    auto* object = new OleBitmapDataObject();

    IDataObject* data_object = nullptr;
    object->QueryInterface(IID_IDataObject,
        reinterpret_cast<void**>(&data_object));

    SetOleBitmapDataObjectBitmap(*object, bitmap);

    IOleClientSite* client_site = nullptr;
    rich_ole->GetClientSite(&client_site);

    ILockBytes* lock_bytes = nullptr;
    throw_if_failed(CreateILockBytesOnHGlobal(nullptr, TRUE, &lock_bytes));

    IStorage* storage = nullptr;
    HRESULT result = StgCreateDocfileOnILockBytes(lock_bytes,
        kStorageCreateFlags, 0, &storage);
    if (FAILED(result)) {
        lock_bytes->Release();
        throw_ole_failure(result);
    }

    IOleObject* ole_object = CreateStaticOleObjectFromBitmapData(*object,
        client_site, storage);
    OleSetContainedObject(ole_object, TRUE);

    REOBJECT reobject{};
    reobject.cbStruct = sizeof(REOBJECT);
    reobject.cp = REO_CP_SELECTION;
    throw_if_failed(ole_object->GetUserClassID(&reobject.clsid));
    reobject.poleobj = ole_object;
    reobject.pstg = storage;
    reobject.polesite = client_site;
    reobject.dvaspect = DVASPECT_CONTENT;
    reobject.dwUser = user_data;
    rich_ole->InsertObject(&reobject);

    release_if_present(ole_object);
    release_if_present(client_site);
    release_if_present(storage);
    data_object->Release();
}

}

#endif
