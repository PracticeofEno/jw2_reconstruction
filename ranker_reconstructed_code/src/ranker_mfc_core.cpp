#include "ranker_mfc_runtime.h"

namespace ranker {

bool ValidateWideStringPointer(const wchar_t* text, std::size_t max_chars) {
    if (text == nullptr) {
        return false;
    }
#ifdef _WIN32
    return IsBadStringPtrW(text, static_cast<UINT_PTR>(max_chars)) == FALSE;
#else
    return true;
#endif
}

bool ValidateAnsiStringPointer(const char* text, std::size_t max_chars) {
    if (text == nullptr) {
        return false;
    }
#ifdef _WIN32
    return IsBadStringPtrA(text, static_cast<UINT_PTR>(max_chars)) == FALSE;
#else
    return true;
#endif
}

bool ValidateMemoryPointer(void* memory, std::size_t bytes, bool writable) {
    if (memory == nullptr) {
        return false;
    }
#ifdef _WIN32
    if (IsBadReadPtr(memory, static_cast<UINT_PTR>(bytes)) != FALSE) {
        return false;
    }
    return !writable || IsBadWritePtr(memory, static_cast<UINT_PTR>(bytes)) == FALSE;
#else
    (void)bytes;
    (void)writable;
    return true;
#endif
}

void InvokeMfcVirtualSlot40Value(MfcObjectCompat& object, unsigned value) {
    (void)object;
    (void)value;
}

void InvokeMfcVirtualSlot40Value1(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 1);
}

void InvokeMfcVirtualSlot40Value16(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 0x10);
}

void InvokeMfcVirtualSlot40Value2048(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 0x800);
}

void InvokeMfcVirtualSlot40Value4(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 4);
}

void InvokeMfcVirtualSlot40Value2(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 2);
}

void InvokeMfcVirtualSlot40Value256(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 0x100);
}

void InvokeMfcVirtualSlot40Value128(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 0x80);
}

void InvokeMfcVirtualSlot40Value32(MfcObjectCompat& object) {
    InvokeMfcVirtualSlot40Value(object, 0x20);
}

} // namespace ranker
