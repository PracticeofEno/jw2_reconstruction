#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <richole.h>
#endif

namespace ranker {

#ifdef _WIN32

class OleBitmapDataObject final : public IDataObject {
public:
    OleBitmapDataObject();
    ~OleBitmapDataObject();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override;
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* format, STGMEDIUM* medium) override;
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override;
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* format,
        FORMATETC* canonical_format) override;
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium,
        BOOL release) override;
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,
        IEnumFORMATETC** enumerator) override;
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* format, DWORD flags,
        IAdviseSink* sink, DWORD* connection) override;
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection) override;
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** enumerator) override;

    const FORMATETC& format() const;
    const STGMEDIUM& medium() const;
    bool releases_stored_medium() const;
    void set_releases_stored_medium(bool release);

private:
    ULONG ref_count_ = 0;
    bool release_stored_medium_ = false;
    STGMEDIUM medium_{};
    FORMATETC format_{};
};

OleBitmapDataObject* InitializeOleBitmapDataObject(OleBitmapDataObject* object);
OleBitmapDataObject* InitializeOleBitmapDataObjectBase(OleBitmapDataObject* object);
OleBitmapDataObject* InitializeOleBitmapDataObjectNoopBase(OleBitmapDataObject* object);
void HandleOleBitmapDataObjectDestructor(OleBitmapDataObject* object);
OleBitmapDataObject* DeleteOleBitmapDataObject(OleBitmapDataObject* object,
    u32 delete_flag);

HRESULT STDMETHODCALLTYPE OleBitmapDataObject_QueryInterface(
    OleBitmapDataObject* object, REFIID iid, void** out_object);
ULONG STDMETHODCALLTYPE OleBitmapDataObject_AddRef(OleBitmapDataObject* object);
ULONG STDMETHODCALLTYPE OleBitmapDataObject_Release(OleBitmapDataObject* object);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_GetData(
    OleBitmapDataObject* object, FORMATETC* format, STGMEDIUM* medium);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_GetDataHere(
    OleBitmapDataObject* object, FORMATETC* format, STGMEDIUM* medium);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_QueryGetData(
    OleBitmapDataObject* object, FORMATETC* format);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_GetCanonicalFormatEtc(
    OleBitmapDataObject* object, FORMATETC* format,
    FORMATETC* canonical_format);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_SetData(
    OleBitmapDataObject* object, FORMATETC* format, STGMEDIUM* medium,
    BOOL release);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_EnumFormatEtc(
    OleBitmapDataObject* object, DWORD direction, IEnumFORMATETC** enumerator);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_DAdvise(
    OleBitmapDataObject* object, FORMATETC* format, DWORD flags,
    IAdviseSink* sink, DWORD* connection);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_DUnadvise(
    OleBitmapDataObject* object, DWORD connection);
HRESULT STDMETHODCALLTYPE OleBitmapDataObject_EnumDAdvise(
    OleBitmapDataObject* object, IEnumSTATDATA** enumerator);

int CompareGuidBytes(const void* left, const void* right);
void SetOleBitmapDataObjectBitmap(OleBitmapDataObject& object, HBITMAP bitmap);
IOleObject* CreateStaticOleObjectFromBitmapData(OleBitmapDataObject& data_object,
    IOleClientSite* client_site, IStorage* storage);
void InsertBitmapAsRichEditOleObject(IRichEditOle* rich_ole, HBITMAP bitmap,
    DWORD user_data);

#endif

}
