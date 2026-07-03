#include "ranker_mfc_runtime.h"

#ifdef _WIN32

#include <algorithm>
#include <cstring>

namespace ranker {

MfcArchiveStreamCompat& InitializeArchiveStreamNoopBase(
    MfcArchiveStreamCompat& stream);

MfcArchiveStreamCompat& ConstructArchiveStream(MfcArchiveStreamCompat& stream,
    void* archive) {
    stream.archive = archive;
    stream.buffer.clear();
    stream.position = 0;
    return stream;
}

ULONG ArchiveStreamAddRef(MfcArchiveStreamCompat&) {
    return 1;
}

ULONG ArchiveStreamRelease(MfcArchiveStreamCompat&) {
    return 0;
}

HRESULT ArchiveStreamQueryInterface(MfcArchiveStreamCompat& stream,
    REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (IsEqualIID(iid, IID_IUnknown) ||
        IsEqualIID(iid, IID_ISequentialStream) ||
        IsEqualIID(iid, IID_IStream)) {
        *object = &stream;
        return S_OK;
    }
    return E_NOINTERFACE;
}

HRESULT ArchiveStreamRead(MfcArchiveStreamCompat& stream, void* data,
    ULONG bytes, ULONG* bytes_read) {
    if (data == nullptr && bytes != 0) {
        return STG_E_INVALIDPOINTER;
    }
    const ULONGLONG available = stream.position < stream.buffer.size()
        ? static_cast<ULONGLONG>(stream.buffer.size()) - stream.position : 0;
    const ULONG count = static_cast<ULONG>(
        std::min<ULONGLONG>(bytes, available));
    if (count != 0) {
        std::memcpy(data, stream.buffer.data() + stream.position, count);
    }
    stream.position += count;
    if (bytes_read != nullptr) {
        *bytes_read = count;
    }
    return count == bytes ? S_OK : S_FALSE;
}

HRESULT ArchiveStreamReadComplete(HRESULT result, ULONG bytes_read,
    ULONG* out_bytes_read) {
    if (out_bytes_read != nullptr) {
        *out_bytes_read = bytes_read;
    }
    return result;
}

HRESULT ArchiveStreamWrite(MfcArchiveStreamCompat& stream, const void* data,
    ULONG bytes, ULONG* bytes_written) {
    if (data == nullptr && bytes != 0) {
        return STG_E_INVALIDPOINTER;
    }
    const ULONGLONG end = stream.position + bytes;
    if (end > stream.buffer.size()) {
        stream.buffer.resize(static_cast<std::size_t>(end));
    }
    if (bytes != 0) {
        std::memcpy(stream.buffer.data() + stream.position, data, bytes);
    }
    stream.position = end;
    if (bytes_written != nullptr) {
        *bytes_written = bytes;
    }
    return S_OK;
}

HRESULT ArchiveStreamWriteComplete(HRESULT result, ULONG bytes_written,
    ULONG* out_bytes_written) {
    if (out_bytes_written != nullptr) {
        *out_bytes_written = bytes_written;
    }
    return result;
}

HRESULT ArchiveStreamSeek(MfcArchiveStreamCompat& stream,
    LARGE_INTEGER distance, DWORD origin, ULARGE_INTEGER* new_position) {
    LONGLONG base = 0;
    if (origin == STREAM_SEEK_SET) {
        base = 0;
    } else if (origin == STREAM_SEEK_CUR) {
        base = static_cast<LONGLONG>(stream.position);
    } else if (origin == STREAM_SEEK_END) {
        base = static_cast<LONGLONG>(stream.buffer.size());
    } else {
        return STG_E_INVALIDFUNCTION;
    }

    const LONGLONG next = base + distance.QuadPart;
    if (next < 0) {
        return STG_E_INVALIDFUNCTION;
    }
    stream.position = static_cast<ULONGLONG>(next);
    if (new_position != nullptr) {
        new_position->QuadPart = stream.position;
    }
    return S_OK;
}

HRESULT ArchiveStreamSeekComplete(HRESULT result, ULONGLONG position,
    ULARGE_INTEGER* new_position) {
    if (new_position != nullptr) {
        new_position->QuadPart = position;
    }
    return result;
}

HRESULT ArchiveStreamSetSizeNotImplemented(MfcArchiveStreamCompat&,
    ULARGE_INTEGER) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamCopyToNotImplemented(MfcArchiveStreamCompat&, IStream*,
    ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamCommitNotImplemented(MfcArchiveStreamCompat&, DWORD) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamRevertNotImplemented(MfcArchiveStreamCompat&) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamLockRegionNotImplemented(MfcArchiveStreamCompat&,
    ULARGE_INTEGER, ULARGE_INTEGER, DWORD) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamUnlockRegionNotImplemented(MfcArchiveStreamCompat&,
    ULARGE_INTEGER, ULARGE_INTEGER, DWORD) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamStatNotImplemented(MfcArchiveStreamCompat&, STATSTG*,
    DWORD) {
    return E_NOTIMPL;
}

HRESULT ArchiveStreamCloneNotImplemented(MfcArchiveStreamCompat&, IStream**) {
    return E_NOTIMPL;
}

MfcArchiveStreamCompat& ConstructArchiveStreamBase(
    MfcArchiveStreamCompat& stream) {
    return InitializeArchiveStreamNoopBase(stream);
}

MfcArchiveStreamCompat& InitializeArchiveStreamNoopBase(
    MfcArchiveStreamCompat& stream) {
    return stream;
}

} // namespace ranker

#endif
