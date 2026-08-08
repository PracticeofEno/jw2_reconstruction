#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"
#include "ranker_dpg_archive.h"
#include "ranker_miles.h"
#include "ranker_win32_compat.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace ranker {

MfcCStringCompat& FillCStringBuffer(MfcCStringCompat& text, char value,
    std::size_t count) {
    text.text.assign(count, value);
    return text;
}

MfcCStringCompat& CopyBytesToCString(MfcCStringCompat& text, const void* data,
    std::size_t count) {
    if (count == 0) {
        text.text.clear();
        return text;
    }
    if (!ValidateMemoryPointer(const_cast<void*>(data), count, false)) {
        CrtDbgReport(2, "strex.cpp", 0x30, nullptr,
            "invalid CString source buffer");
        text.text.clear();
        return text;
    }
    text.text.assign(static_cast<const char*>(data), count);
    return text;
}

MfcCStringCompat& ConvertWideStringToCString(MfcCStringCompat& text,
    const wchar_t* source, int source_chars) {
    text.text.clear();
    if (source == nullptr || source_chars == 0) {
        return text;
    }
#ifdef _WIN32
    const int bytes = WideCharToMultiByte(CP_ACP, 0, source, source_chars,
        nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        text.text.resize(static_cast<std::size_t>(bytes));
        WideCharToMultiByte(CP_ACP, 0, source, source_chars, text.text.data(),
            bytes, nullptr, nullptr);
        if (!text.text.empty() && text.text.back() == '\0') {
            text.text.pop_back();
        }
    }
#else
    (void)source_chars;
#endif
    return text;
}

MfcCStringCompat& LoadCStringResource(MfcCStringCompat& text,
    const char* resource_text) {
    text.text = resource_text == nullptr ? "" : resource_text;
    return text;
}

MfcCStringCompat& AssignCStringChar(MfcCStringCompat& text, char value) {
    text.text.assign(1, value);
    return text;
}

MfcCStringCompat& AssignCStringRepeatedChar(MfcCStringCompat& text, char value,
    int count) {
    text.text.assign(count <= 0 ? 0u : static_cast<std::size_t>(count), value);
    return text;
}

int DeleteCStringRange(MfcCStringCompat& text, int index, int count) {
    if (index < 0) {
        index = 0;
    }
    if (count <= 0 || static_cast<std::size_t>(index) >= text.text.size()) {
        return static_cast<int>(text.text.size());
    }
    const std::size_t remove_count = std::min<std::size_t>(
        static_cast<std::size_t>(count), text.text.size() - index);
    text.text.erase(static_cast<std::size_t>(index), remove_count);
    return static_cast<int>(text.text.size());
}

int InsertCStringChar(MfcCStringCompat& text, int index, char value) {
    if (index < 0) {
        index = 0;
    }
    std::size_t pos = std::min<std::size_t>(static_cast<std::size_t>(index),
        text.text.size());
    text.text.insert(text.text.begin() + static_cast<std::ptrdiff_t>(pos), value);
    return static_cast<int>(text.text.size());
}

int InsertCStringText(MfcCStringCompat& text, int index, const char* value) {
    if (value == nullptr) {
        return static_cast<int>(text.text.size());
    }
    if (index < 0) {
        index = 0;
    }
    std::size_t pos = std::min<std::size_t>(static_cast<std::size_t>(index),
        text.text.size());
    text.text.insert(pos, value);
    return static_cast<int>(text.text.size());
}

int ReplaceCStringChar(MfcCStringCompat& text, char old_value, char new_value) {
    int replaced = 0;
    if (old_value == new_value) {
        return 0;
    }
    for (char& ch : text.text) {
        if (ch == old_value) {
            ch = new_value;
            ++replaced;
        }
    }
    return replaced;
}

int ReplaceCStringSubstring(MfcCStringCompat& text, const char* old_value,
    const char* new_value) {
    if (old_value == nullptr || *old_value == '\0') {
        return 0;
    }
    const std::string needle = old_value;
    const std::string replacement = new_value == nullptr ? "" : new_value;
    int replaced = 0;
    std::size_t pos = 0;
    while ((pos = text.text.find(needle, pos)) != std::string::npos) {
        text.text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
        ++replaced;
    }
    return replaced;
}

int RemoveCStringChar(MfcCStringCompat& text, char value) {
    const std::size_t before = text.text.size();
    text.text.erase(std::remove(text.text.begin(), text.text.end(), value),
        text.text.end());
    return static_cast<int>(before - text.text.size());
}

MfcCStringCompat CStringMidFromIndex(const MfcCStringCompat& text, int index) {
    return CStringMid(text, index, static_cast<int>(text.text.size()) - index);
}

MfcCStringCompat CStringMid(const MfcCStringCompat& text, int index, int count) {
    if (index < 0) {
        index = 0;
    }
    if (count < 0) {
        count = 0;
    }
    MfcCStringCompat out;
    if (static_cast<std::size_t>(index) < text.text.size()) {
        out.text = text.text.substr(static_cast<std::size_t>(index),
            static_cast<std::size_t>(count));
    }
    return out;
}

MfcCStringCompat CStringRight(const MfcCStringCompat& text, int count) {
    if (count < 0) {
        count = 0;
    }
    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(count),
        text.text.size());
    MfcCStringCompat out;
    out.text = text.text.substr(text.text.size() - n);
    return out;
}

MfcCStringCompat CStringLeft(const MfcCStringCompat& text, int count) {
    if (count < 0) {
        count = 0;
    }
    MfcCStringCompat out;
    out.text = text.text.substr(0, static_cast<std::size_t>(count));
    return out;
}

MfcCStringCompat CStringSpanIncluding(const MfcCStringCompat& text,
    const char* chars) {
    MfcCStringCompat out;
    out.text = text.text.substr(0, text.text.find_first_not_of(chars == nullptr ? "" : chars));
    return out;
}

MfcCStringCompat CStringSpanExcluding(const MfcCStringCompat& text,
    const char* chars) {
    MfcCStringCompat out;
    out.text = text.text.substr(0, text.text.find_first_of(chars == nullptr ? "" : chars));
    return out;
}

int ReverseFindCStringChar(const MfcCStringCompat& text, char value) {
    const std::size_t pos = text.text.find_last_of(value);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

int FindCStringSubstring(const MfcCStringCompat& text, const char* needle,
    int start) {
    if (needle == nullptr || start < 0 ||
        static_cast<std::size_t>(start) > text.text.size()) {
        return -1;
    }
    const std::size_t pos = text.text.find(needle, static_cast<std::size_t>(start));
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

MfcCStringCompat& FormatCStringV(MfcCStringCompat& text, const char* format,
    va_list args) {
    text.text.clear();
    if (format == nullptr) {
        return text;
    }
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (needed < 0) {
        return text;
    }
    std::vector<char> buffer(static_cast<std::size_t>(needed) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    text.text.assign(buffer.data(), static_cast<std::size_t>(needed));
    return text;
}

MfcCStringCompat& FormatCString(MfcCStringCompat& text, const char* format, ...) {
    va_list args;
    va_start(args, format);
    FormatCStringV(text, format, args);
    va_end(args);
    return text;
}

MfcCStringCompat& FormatCStringFromResource(MfcCStringCompat& text,
    const char* format, ...) {
    va_list args;
    va_start(args, format);
    FormatCStringV(text, format, args);
    va_end(args);
    return text;
}

MfcCStringCompat& FormatMessageCString(MfcCStringCompat& text,
    const char* format, ...) {
    va_list args;
    va_start(args, format);
#ifdef _WIN32
    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_STRING;
    if (FormatMessageA(flags, format, 0, 0, reinterpret_cast<LPSTR>(&buffer),
        0, &args) != 0 && buffer != nullptr) {
        text.text = buffer;
        LocalFree(buffer);
    } else
#endif
    {
        FormatCStringV(text, format, args);
    }
    va_end(args);
    return text;
}

MfcCStringCompat& FormatMessageCStringFromResource(MfcCStringCompat& text,
    const char* format, ...) {
    va_list args;
    va_start(args, format);
    FormatCStringV(text, format, args);
    va_end(args);
    return text;
}

void TrimCStringRightChars(MfcCStringCompat& text, const char* chars) {
    if (chars == nullptr || *chars == '\0') {
        return;
    }
    const std::size_t pos = text.text.find_last_not_of(chars);
    text.text.erase(pos == std::string::npos ? 0 : pos + 1);
}

void TrimCStringRightChar(MfcCStringCompat& text, char value) {
    while (!text.text.empty() && text.text.back() == value) {
        text.text.pop_back();
    }
}

void TrimCStringRightWhitespace(MfcCStringCompat& text) {
    while (!text.text.empty() &&
        std::isspace(static_cast<unsigned char>(text.text.back())) != 0) {
        text.text.pop_back();
    }
}

void TrimCStringLeftChars(MfcCStringCompat& text, const char* chars) {
    if (chars == nullptr || *chars == '\0') {
        return;
    }
    const std::size_t pos = text.text.find_first_not_of(chars);
    text.text.erase(0, pos == std::string::npos ? text.text.size() : pos);
}

void TrimCStringLeftChar(MfcCStringCompat& text, char value) {
    std::size_t pos = 0;
    while (pos < text.text.size() && text.text[pos] == value) {
        ++pos;
    }
    text.text.erase(0, pos);
}

void TrimCStringLeftWhitespace(MfcCStringCompat& text) {
    std::size_t pos = 0;
    while (pos < text.text.size() &&
        std::isspace(static_cast<unsigned char>(text.text[pos])) != 0) {
        ++pos;
    }
    text.text.erase(0, pos);
}

const char* GetCStringNilData() {
    static const char nil[] = "";
    return nil;
}

MfcCStringCompat& ConstructCStringCopy(MfcCStringCompat& destination,
    const MfcCStringCompat& source) {
    destination.text = source.text;
    return destination;
}

MfcCStringDataHeaderCompat CStringDataHeader(const MfcCStringCompat& text) {
    const auto max_int = static_cast<std::size_t>(
        std::numeric_limits<int>::max());
    const int length = static_cast<int>(std::min(text.text.size(), max_int));
    const int capacity = static_cast<int>(std::min(text.text.capacity(), max_int));
    return MfcCStringDataHeaderCompat{1, length, capacity};
}

MfcCStringCompat& ConstructCStringEmpty(MfcCStringCompat& text) {
    text.text.clear();
    return text;
}

MfcCStringCompat& ConstructCStringEmptyAndReturn(MfcCStringCompat& text) {
    return ConstructCStringEmpty(text);
}

MfcCStringCompat& ConstructCStringFromAnsiInline(MfcCStringCompat& text,
    const char* source) {
    ConstructCStringEmpty(text);
    return AssignCStringAnsi(text, source);
}

MfcCStringCompat& AssignCStringAnsiInline(MfcCStringCompat& text,
    const char* source) {
    return AssignCStringAnsi(text, source);
}

MfcCStringCompat& AssignCStringAnsiChecked(MfcCStringCompat& text,
    const char* source) {
    if (!ValidateAnsiStringPointer(source, static_cast<std::size_t>(-1))) {
        AfxTraceOutput("afx.inl: invalid CString ANSI source pointer.\n");
        text.text.clear();
        return text;
    }
    return AssignCStringAnsi(text, source);
}

int CStringDataLength(const MfcCStringCompat& text) {
    return CStringDataHeader(text).data_length;
}

int CStringAllocLength(const MfcCStringCompat& text) {
    return CStringDataHeader(text).alloc_length;
}

bool CStringIsEmptyInline(const MfcCStringCompat& text) {
    return CStringDataLength(text) == 0;
}

const char* CStringGetStringPtr(const MfcCStringCompat& text) {
    return text.text.empty() ? GetCStringNilData() : text.text.c_str();
}

int SafeAnsiStringLength(const char* text) {
    if (text == nullptr) {
        return 0;
    }
#ifdef _WIN32
    return lstrlenA(text);
#else
    return static_cast<int>(std::strlen(text));
#endif
}

int CStringCompareDirectoryTraversalAnsi(const MfcCStringCompat& text,
    const char* other) {
    if (!ValidateAnsiStringPointer(other, static_cast<std::size_t>(-1))) {
        AfxTraceOutput("afx.inl: invalid CString compare source pointer.\n");
    }
    return CompareDirectoryTraversalName(CStringGetStringPtr(text), other);
}

int CStringCompareBriefingArchiveAnsi(const MfcCStringCompat& text,
    const char* other) {
    if (!ValidateAnsiStringPointer(other, static_cast<std::size_t>(-1))) {
        AfxTraceOutput("afx.inl: invalid CString compare source pointer.\n");
    }
    return CompareBriefingBinkArchiveName(CStringGetStringPtr(text), other);
}

int CStringCollateAnsi(const MfcCStringCompat& text, const char* other) {
    if (!ValidateAnsiStringPointer(other, static_cast<std::size_t>(-1))) {
        AfxTraceOutput("afx.inl: invalid CString collate source pointer.\n");
    }
    return CompareMbcsString(CStringGetStringPtr(text), other);
}

int CStringCollateNoCaseAnsi(const MfcCStringCompat& text, const char* other) {
    if (!ValidateAnsiStringPointer(other, static_cast<std::size_t>(-1))) {
        AfxTraceOutput("afx.inl: invalid CString collate source pointer.\n");
    }
    return CompareMbcsCaseInsensitive(CStringGetStringPtr(text), other);
}

char CStringGetAt(const MfcCStringCompat& text, int index) {
    if (index < 0 || index >= CStringDataLength(text)) {
        AfxTraceOutput("afx.inl: CString index %d out of bounds.\n", index);
        return '\0';
    }
    return text.text[static_cast<std::size_t>(index)];
}

char CStringSubscript(const MfcCStringCompat& text, int index) {
    return CStringGetAt(text, index);
}

bool CStringEqualsCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    return CStringCompareDirectoryTraversalAnsi(left,
        CStringGetStringPtr(right)) == 0;
}

bool CStringEqualsAnsi(const MfcCStringCompat& left, const char* right) {
    return CStringCompareDirectoryTraversalAnsi(left, right) == 0;
}

bool CStringAnsiEqualsCString(const char* left, const MfcCStringCompat& right) {
    return CStringCompareDirectoryTraversalAnsi(right, left) == 0;
}

bool CStringNotEqualsCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    return !CStringEqualsCString(left, right);
}

bool CStringNotEqualsAnsi(const MfcCStringCompat& left, const char* right) {
    return !CStringEqualsAnsi(left, right);
}

bool CStringAnsiNotEqualsCString(const char* left,
    const MfcCStringCompat& right) {
    return !CStringAnsiEqualsCString(left, right);
}

bool CStringLessThanCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    return CStringCompareDirectoryTraversalAnsi(left,
        CStringGetStringPtr(right)) < 0;
}

bool CStringLessThanAnsi(const MfcCStringCompat& left, const char* right) {
    return CStringCompareDirectoryTraversalAnsi(left, right) < 0;
}

bool CStringAnsiLessThanCString(const char* left,
    const MfcCStringCompat& right) {
    return CompareDirectoryTraversalName(left, CStringGetStringPtr(right)) < 0;
}

bool CStringGreaterThanCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    return CStringCompareDirectoryTraversalAnsi(left,
        CStringGetStringPtr(right)) > 0;
}

bool CStringGreaterThanAnsi(const MfcCStringCompat& left, const char* right) {
    return CStringCompareDirectoryTraversalAnsi(left, right) > 0;
}

bool CStringAnsiGreaterThanCString(const char* left,
    const MfcCStringCompat& right) {
    return CompareDirectoryTraversalName(left, CStringGetStringPtr(right)) > 0;
}

bool CStringLessEqualCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    return !CStringGreaterThanCString(left, right);
}

bool CStringLessEqualAnsi(const MfcCStringCompat& left, const char* right) {
    return !CStringGreaterThanAnsi(left, right);
}

bool CStringAnsiLessEqualCString(const char* left,
    const MfcCStringCompat& right) {
    return !CStringAnsiGreaterThanCString(left, right);
}

bool CStringGreaterEqualCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    return !CStringLessThanCString(left, right);
}

bool CStringGreaterEqualAnsi(const MfcCStringCompat& left, const char* right) {
    return !CStringLessThanAnsi(left, right);
}

bool CStringAnsiGreaterEqualCString(const char* left,
    const MfcCStringCompat& right) {
    return !CStringAnsiLessThanCString(left, right);
}

void AllocCStringBuffer(MfcCStringCompat& text, int length) {
    if (length < 0) {
        length = 0;
    }
    text.text.assign(static_cast<std::size_t>(length), '\0');
}

void FreeCStringData(void* data) {
    if (data != nullptr) {
        CrtDebugHeapFree(data);
    }
}

void ReleaseCString(MfcCStringCompat& text) {
    text.text.clear();
}

void ReleaseCStringData(void* data) {
    FreeCStringData(data);
}

void EmptyCString(MfcCStringCompat& text) {
    text.text.clear();
}

void CStringCopyBeforeWrite(MfcCStringCompat& text) {
    text.text.reserve(text.text.size());
}

void CStringAllocBeforeWrite(MfcCStringCompat& text, int length) {
    if (length < 0) {
        length = 0;
    }
    text.text.reserve(static_cast<std::size_t>(length));
}

void ReleaseCStringBuffer(MfcCStringCompat& text) {
    text.text.clear();
}

void AllocCopyCString(const MfcCStringCompat& source, MfcCStringCompat& target,
    int copy_length, int copy_index, int extra_length) {
    if (copy_index < 0) {
        copy_index = 0;
    }
    if (copy_length < 0) {
        copy_length = 0;
    }
    if (extra_length < 0) {
        extra_length = 0;
    }
    const std::size_t start = std::min<std::size_t>(
        static_cast<std::size_t>(copy_index), source.text.size());
    const std::size_t count = std::min<std::size_t>(
        static_cast<std::size_t>(copy_length), source.text.size() - start);
    target.text.assign(source.text.data() + start, count);
    target.text.reserve(count + static_cast<std::size_t>(extra_length));
}

MfcCStringCompat& ConstructCStringFromAnsiOrResource(MfcCStringCompat& text,
    const char* source) {
    text.text = source == nullptr ? "" : source;
    return text;
}

MfcCStringCompat& ConstructCStringFromWide(MfcCStringCompat& text,
    const wchar_t* source) {
    if (source == nullptr) {
        text.text.clear();
        return text;
    }
#ifdef _WIN32
    return ConvertWideStringToCString(text, source, -1);
#else
    text.text.clear();
    while (*source != L'\0') {
        text.text.push_back(static_cast<char>(*source++ & 0xff));
    }
    return text;
#endif
}

void ArchiveSerializeCString(void* archive, MfcCStringCompat& text) {
    auto* typed_archive = static_cast<MfcArchiveCompat*>(archive);
    if (typed_archive == nullptr) {
        return;
    }
    if (typed_archive->storing) {
        ArchiveWriteCString(*typed_archive, text);
    } else {
        ArchiveReadCString(*typed_archive, text);
    }
}

void AssignCopyCString(MfcCStringCompat& text, int length, const char* source) {
    if (source == nullptr || length <= 0) {
        text.text.clear();
        return;
    }
    text.text.assign(source, static_cast<std::size_t>(length));
}

MfcCStringCompat& AssignCStringCopy(MfcCStringCompat& text,
    const MfcCStringCompat& source) {
    if (&text != &source) {
        text.text = source.text;
    }
    return text;
}

MfcCStringCompat& AssignCStringAnsi(MfcCStringCompat& text, const char* source) {
    text.text = source == nullptr ? "" : source;
    return text;
}

MfcCStringCompat& AssignCStringWide(MfcCStringCompat& text,
    const wchar_t* source) {
    return ConstructCStringFromWide(text, source);
}

void ConcatCopyCString(MfcCStringCompat& text, int left_length,
    const char* left, int right_length, const char* right) {
    if (left_length < 0) {
        left_length = 0;
    }
    if (right_length < 0) {
        right_length = 0;
    }
    text.text.clear();
    if (left != nullptr && left_length != 0) {
        text.text.append(left, static_cast<std::size_t>(left_length));
    }
    if (right != nullptr && right_length != 0) {
        text.text.append(right, static_cast<std::size_t>(right_length));
    }
}

MfcCStringCompat ConcatCStringStrings(const MfcCStringCompat& left,
    const MfcCStringCompat& right) {
    MfcCStringCompat result;
    result.text.reserve(left.text.size() + right.text.size());
    result.text.append(left.text);
    result.text.append(right.text);
    return result;
}

MfcCStringCompat ConcatCStringAndAnsi(const MfcCStringCompat& left,
    const char* right) {
    MfcCStringCompat result;
    result.text = left.text;
    if (right != nullptr) {
        result.text.append(right);
    }
    return result;
}

MfcCStringCompat ConcatAnsiAndCString(const char* left,
    const MfcCStringCompat& right) {
    MfcCStringCompat result;
    if (left != nullptr) {
        result.text = left;
    }
    result.text.append(right.text);
    return result;
}

void AppendCStringRaw(MfcCStringCompat& text, int length, const char* source) {
    if (source != nullptr && length > 0) {
        text.text.append(source, static_cast<std::size_t>(length));
    }
}

MfcCStringCompat& AppendCStringAnsi(MfcCStringCompat& text, const char* source) {
    if (source != nullptr) {
        text.text.append(source);
    }
    return text;
}

MfcCStringCompat& AppendCStringChar(MfcCStringCompat& text, char value) {
    text.text.push_back(value);
    return text;
}

MfcCStringCompat& AppendCString(MfcCStringCompat& text,
    const MfcCStringCompat& source) {
    text.text.append(source.text);
    return text;
}

char* CStringGetBuffer(MfcCStringCompat& text, int min_length) {
    if (min_length < 0) {
        min_length = 0;
    }
    if (text.text.size() < static_cast<std::size_t>(min_length)) {
        text.text.resize(static_cast<std::size_t>(min_length), '\0');
    }
    return text.text.empty() ? const_cast<char*>(GetCStringNilData())
                             : text.text.data();
}

void CStringReleaseBuffer(MfcCStringCompat& text, int new_length) {
    if (new_length < 0) {
        const std::size_t nul = text.text.find('\0');
        if (nul != std::string::npos) {
            text.text.resize(nul);
        }
        return;
    }
    text.text.resize(static_cast<std::size_t>(new_length), '\0');
}

char* CStringGetBufferSetLength(MfcCStringCompat& text, int new_length) {
    if (new_length < 0) {
        new_length = 0;
    }
    text.text.resize(static_cast<std::size_t>(new_length), '\0');
    return text.text.empty() ? const_cast<char*>(GetCStringNilData())
                             : text.text.data();
}

void CStringFreeExtra(MfcCStringCompat& text) {
    text.text.shrink_to_fit();
}

char* CStringLockBuffer(MfcCStringCompat& text) {
    return CStringGetBuffer(text, static_cast<int>(text.text.size()));
}

void CStringUnlockBuffer(MfcCStringCompat& text) {
    (void)text;
}

int CStringFindChar(const MfcCStringCompat& text, char value) {
    return CStringFindCharFrom(text, value, 0);
}

int CStringFindCharFrom(const MfcCStringCompat& text, char value, int start) {
    if (start < 0) {
        start = 0;
    }
    if (static_cast<std::size_t>(start) >= text.text.size()) {
        return -1;
    }
    const std::size_t found = text.text.find(value, static_cast<std::size_t>(start));
    return found == std::string::npos ? -1 : static_cast<int>(found);
}

int CStringFindOneOf(const MfcCStringCompat& text, const char* chars) {
    if (chars == nullptr) {
        return -1;
    }
    const char* found = FindCStringOneOf(text.text.c_str(), chars);
    return found == nullptr ? -1 : static_cast<int>(found - text.text.c_str());
}

void CStringMakeLower(MfcCStringCompat& text) {
    CStringCopyBeforeWrite(text);
    if (!text.text.empty()) {
        LowercaseCStringInPlace(text.text.data());
    }
}

void CStringMakeUpper(MfcCStringCompat& text) {
    CStringCopyBeforeWrite(text);
    if (!text.text.empty()) {
        UppercaseMbcsStringInPlace(text.text.data());
    }
}

void CStringMakeReverse(MfcCStringCompat& text) {
    CStringCopyBeforeWrite(text);
    if (!text.text.empty()) {
        ReverseMbcsStringInPlace(text.text.data());
    }
}

void CStringSetAt(MfcCStringCompat& text, int index, char value) {
    if (index < 0 || static_cast<std::size_t>(index) >= text.text.size()) {
        return;
    }
    CStringCopyBeforeWrite(text);
    text.text[static_cast<std::size_t>(index)] = value;
}

void CStringAnsiToOem(MfcCStringCompat& text) {
    CStringCopyBeforeWrite(text);
#ifdef _WIN32
    if (!text.text.empty()) {
        CharToOemA(text.text.c_str(), text.text.data());
    }
#endif
}

void CStringOemToAnsi(MfcCStringCompat& text) {
    CStringCopyBeforeWrite(text);
#ifdef _WIN32
    if (!text.text.empty()) {
        OemToCharA(text.text.c_str(), text.text.data());
    }
#endif
}

int WideCharToAnsiCounted(char* destination, const wchar_t* source,
    int destination_chars) {
    if (destination_chars == 0 && destination != nullptr) {
        return 0;
    }
    if (source == nullptr) {
        if (destination != nullptr && destination_chars > 0) {
            destination[0] = '\0';
        }
        return 0;
    }
#ifdef _WIN32
    const int result = WideCharToMultiByte(CP_ACP, 0, source, -1, destination,
        destination_chars, nullptr, nullptr);
    if (result > 0 && destination != nullptr) {
        destination[result - 1] = '\0';
    }
    return result;
#else
    int written = 0;
    while (source[written] != L'\0' &&
        (destination == nullptr || written + 1 < destination_chars)) {
        if (destination != nullptr) {
            destination[written] = static_cast<char>(source[written] & 0xff);
        }
        ++written;
    }
    if (destination != nullptr && destination_chars > 0) {
        destination[written] = '\0';
    }
    return written + 1;
#endif
}

int AnsiToWideCounted(wchar_t* destination, const char* source,
    int destination_chars) {
    if (destination_chars == 0 && destination != nullptr) {
        return 0;
    }
    if (source == nullptr) {
        if (destination != nullptr && destination_chars > 0) {
            destination[0] = L'\0';
        }
        return 0;
    }
#ifdef _WIN32
    const int result = MultiByteToWideChar(CP_ACP, 0, source, -1, destination,
        destination_chars);
    if (result > 0 && destination != nullptr) {
        destination[result - 1] = L'\0';
    }
    return result;
#else
    int written = 0;
    while (source[written] != '\0' &&
        (destination == nullptr || written + 1 < destination_chars)) {
        if (destination != nullptr) {
            destination[written] = static_cast<unsigned char>(source[written]);
        }
        ++written;
    }
    if (destination != nullptr && destination_chars > 0) {
        destination[written] = L'\0';
    }
    return written + 1;
#endif
}

wchar_t* AfxAnsiToWideHelper(wchar_t* destination, const char* source,
    int destination_chars) {
    if (source == nullptr) {
        return nullptr;
    }
    if (destination == nullptr) {
        return nullptr;
    }
    destination[0] = L'\0';
    return AnsiToWideCounted(destination, source, destination_chars) == 0
        ? nullptr : destination;
}

char* AfxWideToAnsiHelper(char* destination, const wchar_t* source,
    int destination_chars) {
    if (source == nullptr) {
        return nullptr;
    }
    if (destination == nullptr) {
        return nullptr;
    }
    destination[0] = '\0';
    return WideCharToAnsiCounted(destination, source, destination_chars) == 0
        ? nullptr : destination;
}

void ConstructCStringArrayElements(std::vector<MfcCStringCompat>& values,
    std::size_t count) {
    values.assign(count, MfcCStringCompat{});
}

void DestroyCStringArrayElements(std::vector<MfcCStringCompat>& values) {
    values.clear();
}

void CopyCStringArrayElements(std::vector<MfcCStringCompat>& destination,
    const std::vector<MfcCStringCompat>& source, std::size_t count) {
    destination.assign(source.begin(),
        source.begin() + static_cast<std::ptrdiff_t>(std::min(count, source.size())));
}

unsigned HashWideStringKey(const wchar_t* text) {
    unsigned hash = 0;
    if (text == nullptr) {
        return hash;
    }
    while (*text != L'\0') {
        hash = hash * 0x21u + static_cast<unsigned>(*text++);
    }
    return hash;
}

} // namespace ranker
