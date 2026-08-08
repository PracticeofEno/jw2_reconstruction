#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"
#include "ranker_win32_compat.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <shlobj.h>
#endif

namespace ranker {

#ifdef _WIN32
namespace {

bool is_dir_sep_char(char value) {
    return value == '\\' || value == '/';
}

std::string full_path_name(const char* path) {
    if (path == nullptr || *path == '\0') {
        return {};
    }
    char buffer[MAX_PATH]{};
    if (GetFullPathNameA(path, static_cast<DWORD>(sizeof(buffer)), buffer,
            nullptr) == 0) {
        return path;
    }
    return buffer;
}

std::string recent_profile_path() {
    std::string profile = "ranker.ini";
    auto* thread = AfxGetThreadCompat();
    auto* app = thread != nullptr && thread->runtime_class == GetWinAppRuntimeClass()
        ? static_cast<MfcWinAppCompat*>(thread) : nullptr;
    if (app != nullptr && !app->profile_name.empty()) {
        profile = app->profile_name;
    }
    if (profile.find(':') == std::string::npos &&
        profile.find('\\') == std::string::npos &&
        profile.find('/') == std::string::npos &&
        profile.find('.') == std::string::npos) {
        profile += ".ini";
    }
    return profile;
}

std::string format_recent_key(const MfcRecentFileListCompat& recent,
    int index) {
    char key[64]{};
    const char* format = recent.entry_format.empty()
        ? "File%d" : recent.entry_format.c_str();
    std::snprintf(key, sizeof(key), format, index + 1);
    return key;
}

std::string escape_menu_ampersands(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char value : text) {
        if (value == '&') {
            result.push_back('&');
        }
        result.push_back(value);
    }
    return result;
}
} // namespace

void AbbreviatePathName(char* path, int max_chars, bool keep_at_least_name) {
    if (path == nullptr || max_chars <= 0) {
        return;
    }
    const int length = lstrlenA(path);
    if (length <= max_chars) {
        return;
    }

    char* file_name = path + length;
    while (file_name > path && !is_dir_sep_char(file_name[-1])) {
        --file_name;
    }
    const int file_length = lstrlenA(file_name);
    if (max_chars < file_length) {
        lstrcpyA(path, keep_at_least_name ? file_name : "");
        return;
    }

    char* root_end = path;
    if (is_dir_sep_char(path[0]) && is_dir_sep_char(path[1])) {
        root_end = path + 2;
        while (*root_end != '\0' && !is_dir_sep_char(*root_end)) {
            ++root_end;
        }
        if (*root_end != '\0') {
            ++root_end;
            while (*root_end != '\0' && !is_dir_sep_char(*root_end)) {
                ++root_end;
            }
        }
    } else if (std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':') {
        root_end = path + 2;
        if (is_dir_sep_char(*root_end)) {
            ++root_end;
        }
    } else if (is_dir_sep_char(path[0])) {
        root_end = path + 1;
    }

    if (root_end <= path) {
        lstrcpyA(path, file_name);
        return;
    }
    while (*root_end != '\0' &&
        max_chars < static_cast<int>((root_end - path) + 4 + lstrlenA(root_end))) {
        ++root_end;
        while (*root_end != '\0' && !is_dir_sep_char(*root_end)) {
            ++root_end;
        }
    }
    if (*root_end == '\0') {
        lstrcpyA(path, file_name);
        return;
    }
    const std::string suffix = root_end;
    *root_end = '\0';
    lstrcatA(path, "...");
    lstrcatA(path, suffix.c_str());
}

MfcRecentFileListCompat& ConstructRecentFileList(
    MfcRecentFileListCompat& recent, unsigned start, const char* section_name,
    const char* entry_format, int max_size, int max_display_length) {
    recent.runtime_class = GetCObjectRuntimeClass();
    recent.start = start;
    recent.max_size = max_size > 0 ? max_size : 0;
    recent.max_display_length = max_display_length;
    recent.section_name = section_name == nullptr ? "" : section_name;
    recent.entry_format = entry_format == nullptr ? "" : entry_format;
    recent.names.assign(static_cast<std::size_t>(recent.max_size), std::string{});
    return recent;
}

void DestroyRecentFileList(MfcRecentFileListCompat& recent) {
    recent.names.clear();
    recent.section_name.clear();
    recent.entry_format.clear();
    recent.max_size = 0;
    recent.start = 0;
    recent.max_display_length = -1;
    recent.runtime_class = nullptr;
}

void RecentFileListAdd(MfcRecentFileListCompat& recent, const char* path) {
    if (path == nullptr || recent.max_size <= 0) {
        return;
    }
    std::string full_path = full_path_name(path);
    if (full_path.empty()) {
        return;
    }
    auto same_path = [&](const std::string& value) {
        return !value.empty() && lstrcmpiA(value.c_str(), full_path.c_str()) == 0;
    };
    auto found = std::find_if(recent.names.begin(), recent.names.end(), same_path);
    if (found != recent.names.end()) {
        recent.names.erase(found);
    }
    recent.names.insert(recent.names.begin(), full_path);
    recent.names.resize(static_cast<std::size_t>(recent.max_size));
}

void RecentFileListRemove(MfcRecentFileListCompat& recent, int index) {
    if (index < 0 || index >= static_cast<int>(recent.names.size())) {
        return;
    }
    recent.names.erase(recent.names.begin() + index);
    recent.names.push_back(std::string{});
    if (recent.max_size >= 0 &&
        static_cast<int>(recent.names.size()) > recent.max_size) {
        recent.names.resize(static_cast<std::size_t>(recent.max_size));
    }
}

bool RecentFileListGetDisplayName(MfcRecentFileListCompat& recent,
    std::string& display, int index, const char* directory,
    int directory_length, bool keep_at_least_name) {
    if (index < 0 || index >= static_cast<int>(recent.names.size()) ||
        recent.names[index].empty()) {
        display.clear();
        return false;
    }
    display = recent.names[index];
    if (directory != nullptr && directory_length > 0 &&
        static_cast<int>(display.size()) > directory_length &&
        _strnicmp(display.c_str(), directory, directory_length) == 0) {
        display.erase(0, static_cast<std::size_t>(directory_length));
    }
    if (recent.max_display_length != -1 &&
        static_cast<int>(display.size()) > recent.max_display_length) {
        std::vector<char> buffer(MAX_PATH, 0);
        lstrcpynA(buffer.data(), display.c_str(), static_cast<int>(buffer.size()));
        AbbreviatePathName(buffer.data(), recent.max_display_length,
            keep_at_least_name);
        display = buffer.data();
    }
    return true;
}

void RecentFileListUpdateMenu(MfcRecentFileListCompat& recent,
    MfcCmdUICompat& cmd_ui) {
    if (cmd_ui.menu == nullptr || recent.max_size <= 0) {
        CmdUIEnable(cmd_ui, false);
        return;
    }
    char current_directory[MAX_PATH]{};
    DWORD current_len = GetCurrentDirectoryA(MAX_PATH, current_directory);
    if (current_len != 0 && current_len < MAX_PATH) {
        const std::size_t len = std::strlen(current_directory);
        if (len + 1 < MAX_PATH && !is_dir_sep_char(current_directory[len - 1])) {
            current_directory[len] = '\\';
            current_directory[len + 1] = '\0';
            ++current_len;
        }
    }

    int inserted = 0;
    for (int index = 0; index < recent.max_size; ++index) {
        std::string display;
        if (!RecentFileListGetDisplayName(recent, display, index,
                current_directory, static_cast<int>(current_len), true)) {
            continue;
        }
        display = escape_menu_ampersands(display);
        char item_text[MAX_PATH + 16]{};
        std::snprintf(item_text, sizeof(item_text), "&%u %s",
            (recent.start + static_cast<unsigned>(inserted + 1)) % 10,
            display.c_str());
        const UINT command_id = recent.start + static_cast<UINT>(inserted);
        if (inserted == 0) {
            ModifyMenuA(cmd_ui.menu, cmd_ui.index, MF_BYPOSITION | MF_STRING,
                command_id, item_text);
        } else {
            InsertMenuA(cmd_ui.menu, cmd_ui.index + static_cast<UINT>(inserted),
                MF_BYPOSITION | MF_STRING, command_id, item_text);
        }
        ++inserted;
    }
    if (inserted == 0) {
        CmdUIEnable(cmd_ui, false);
    } else {
        cmd_ui.menu_item_count = static_cast<UINT>(inserted);
        CmdUIEnable(cmd_ui, true);
    }
}

void RecentFileListWriteList(MfcRecentFileListCompat& recent) {
    const std::string profile = recent_profile_path();
    const char* section = recent.section_name.empty()
        ? "Recent File List" : recent.section_name.c_str();
    for (int index = 0; index < recent.max_size; ++index) {
        const std::string key = format_recent_key(recent, index);
        const char* value = index < static_cast<int>(recent.names.size()) &&
            !recent.names[index].empty() ? recent.names[index].c_str() : nullptr;
        WritePrivateProfileStringA(section, key.c_str(), value, profile.c_str());
    }
}

void RecentFileListReadList(MfcRecentFileListCompat& recent) {
    if (recent.max_size <= 0) {
        return;
    }
    recent.names.assign(static_cast<std::size_t>(recent.max_size), std::string{});
    const std::string profile = recent_profile_path();
    const char* section = recent.section_name.empty()
        ? "Recent File List" : recent.section_name.c_str();
    for (int index = 0; index < recent.max_size; ++index) {
        const std::string key = format_recent_key(recent, index);
        char value[MAX_PATH]{};
        GetPrivateProfileStringA(section, key.c_str(), "", value,
            static_cast<DWORD>(sizeof(value)), profile.c_str());
        if (value[0] != '\0') {
            recent.names[static_cast<std::size_t>(index)] = value;
        }
    }
}

MfcRecentFileListCompat* DeleteRecentFileListScalarDtor(
    MfcRecentFileListCompat* recent, unsigned flags) {
    if (recent == nullptr) {
        return nullptr;
    }
    DestroyRecentFileList(*recent);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteNormalBlock(recent);
        return nullptr;
    }
    return recent;
}

void* DeleteCStringVectorHelper(MfcCStringCompat* values, unsigned flags) {
    if (values == nullptr) {
        return nullptr;
    }
    if ((flags & 2U) == 0) {
        values->~MfcCStringCompat();
        if ((flags & 1U) != 0) {
            MfcDebugDeleteNormalBlock(values);
        }
        return values;
    }
    int count = *(reinterpret_cast<int*>(values) - 1);
    for (int index = 0; index < count; ++index) {
        values[index].~MfcCStringCompat();
    }
    void* block = reinterpret_cast<int*>(values) - 1;
    if ((flags & 1U) != 0) {
        MfcDebugDeleteNormalBlock(block);
    }
    return block;
}

[[noreturn]] void ThrowLastFileError(const char* file_name) {
    const DWORD error = GetLastError();
    ThrowFileException(FileExceptionCauseFromOsError(error), error, file_name);
}

MfcFileCompat& ConstructFileDefault(MfcFileCompat& file) {
    file.runtime_class = GetCObjectRuntimeClass();
    file.handle = INVALID_HANDLE_VALUE;
    file.close_on_delete = false;
    file.file_name.clear();
    return file;
}

MfcFileCompat& ConstructFileFromHandle(MfcFileCompat& file, HANDLE handle) {
    ConstructFileDefault(file);
    file.handle = handle;
    file.close_on_delete = false;
    return file;
}

MfcFileCompat& ConstructFileFromPath(MfcFileCompat& file, const char* path,
    unsigned open_flags) {
    ConstructFileDefault(file);
    MfcFileExceptionCompat exception{};
    if (!FileOpen(file, path, open_flags, &exception)) {
        ThrowFileException(exception.cause, exception.os_error,
            exception.file_name.c_str());
    }
    return file;
}

MfcFileCompat* DuplicateFileCompat(const MfcFileCompat& file) {
    FileAssertValid(file);
    if (file.handle == INVALID_HANDLE_VALUE) {
        ThrowFileException(6, ERROR_INVALID_HANDLE, file.file_name.c_str());
    }
    auto* duplicate = new MfcFileCompat();
    ConstructFileFromHandle(*duplicate, INVALID_HANDLE_VALUE);
    HANDLE duplicated = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), file.handle, GetCurrentProcess(),
            &duplicated, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        delete duplicate;
        ThrowLastFileError(file.file_name.c_str());
    }
    duplicate->handle = duplicated;
    duplicate->close_on_delete = file.close_on_delete;
    duplicate->file_name = file.file_name;
    return duplicate;
}

bool FileOpen(MfcFileCompat& file, const char* path, unsigned open_flags,
    MfcFileExceptionCompat* exception) {
    FileAssertValid(file);
    if (path == nullptr) {
        path = "";
    }
    if ((open_flags & 0x4000U) != 0) {
        if (exception != nullptr) {
            exception->cause = 1;
            exception->os_error = ERROR_INVALID_PARAMETER;
            exception->file_name = path;
        }
        return false;
    }

    file.handle = INVALID_HANDLE_VALUE;
    file.close_on_delete = false;
    char full_path[MAX_PATH]{};
    AfxFullPath(full_path, path);
    file.file_name = full_path[0] == '\0' ? path : full_path;

    DWORD desired_access = GENERIC_READ;
    switch (open_flags & 3U) {
    case 0:
        desired_access = GENERIC_READ;
        break;
    case 1:
        desired_access = GENERIC_WRITE;
        break;
    case 2:
        desired_access = GENERIC_READ | GENERIC_WRITE;
        break;
    default:
        desired_access = GENERIC_READ;
        break;
    }

    DWORD share_mode = 0;
    switch (open_flags & 0x70U) {
    case 0x20:
        share_mode = FILE_SHARE_READ;
        break;
    case 0x30:
        share_mode = FILE_SHARE_WRITE;
        break;
    case 0x40:
        share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    case 0:
    case 0x10:
    default:
        share_mode = 0;
        break;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = (open_flags & 0x80U) == 0 ? TRUE : FALSE;
    DWORD creation = OPEN_EXISTING;
    if ((open_flags & 0x1000U) != 0) {
        creation = (open_flags & 0x2000U) != 0 ? OPEN_ALWAYS : CREATE_ALWAYS;
    }

    HANDLE handle = CreateFileA(path, desired_access, share_mode, &security,
        creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (exception != nullptr) {
            const DWORD error = GetLastError();
            exception->cause = FileExceptionCauseFromOsError(error);
            exception->os_error = static_cast<long>(error);
            exception->file_name = path;
        }
        return false;
    }
    file.handle = handle;
    file.close_on_delete = true;
    return true;
}

unsigned FileRead(MfcFileCompat& file, void* buffer, unsigned bytes) {
    FileAssertValid(file);
    if (bytes == 0) {
        return 0;
    }
    DWORD read = 0;
    if (!ReadFile(file.handle, buffer, bytes, &read, nullptr)) {
        ThrowLastFileError(file.file_name.c_str());
    }
    return read;
}

void FileWrite(MfcFileCompat& file, const void* buffer, unsigned bytes) {
    FileAssertValid(file);
    if (bytes == 0) {
        return;
    }
    DWORD written = 0;
    if (!WriteFile(file.handle, buffer, bytes, &written, nullptr)) {
        ThrowLastFileError(file.file_name.c_str());
    }
    if (written != bytes) {
        ThrowFileException(13, ERROR_HANDLE_DISK_FULL, file.file_name.c_str());
    }
}

void FileWriteHuge(MfcFileCompat& file, const void* buffer,
    unsigned long bytes) {
    FileWrite(file, buffer, static_cast<unsigned>(bytes));
}

unsigned long FileSeek(MfcFileCompat& file, long offset, unsigned origin) {
    FileAssertValid(file);
    DWORD result = SetFilePointer(file.handle, offset, nullptr, origin);
    if (result == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        ThrowLastFileError(file.file_name.c_str());
    }
    return result;
}

unsigned long FileSeekToEnd(MfcFileCompat& file) {
    return FileSeek(file, 0, FILE_END);
}

void FileSeekToBegin(MfcFileCompat& file) {
    FileSeek(file, 0, FILE_BEGIN);
}

unsigned long FileGetPosition(MfcFileCompat& file) {
    return FileSeek(file, 0, FILE_CURRENT);
}

void FileFlush(MfcFileCompat& file) {
    FileAssertValid(file);
    if (file.handle != INVALID_HANDLE_VALUE && !FlushFileBuffers(file.handle)) {
        ThrowLastFileError(file.file_name.c_str());
    }
}

void FileClose(MfcFileCompat& file) {
    FileAssertValid(file);
    const HANDLE handle = file.handle;
    file.handle = INVALID_HANDLE_VALUE;
    file.close_on_delete = false;
    file.file_name.clear();
    if (handle != INVALID_HANDLE_VALUE && !CloseHandle(handle)) {
        ThrowLastFileError(nullptr);
    }
}

void FileDestructor(MfcFileCompat& file) {
    if (file.handle != INVALID_HANDLE_VALUE && file.close_on_delete) {
        FileClose(file);
    } else {
        file.handle = INVALID_HANDLE_VALUE;
        file.close_on_delete = false;
        file.file_name.clear();
    }
    DestroyCObject(file);
}

void FileAbort(MfcFileCompat& file) {
    FileAssertValid(file);
    if (file.handle != INVALID_HANDLE_VALUE) {
        CloseHandle(file.handle);
        file.handle = INVALID_HANDLE_VALUE;
    }
    file.close_on_delete = false;
    file.file_name.clear();
}

void FileLockRange(MfcFileCompat& file, unsigned long position,
    unsigned long count) {
    FileAssertValid(file);
    if (!LockFile(file.handle, position, 0, count, 0)) {
        ThrowLastFileError(file.file_name.c_str());
    }
}

void FileUnlockRange(MfcFileCompat& file, unsigned long position,
    unsigned long count) {
    FileAssertValid(file);
    if (!UnlockFile(file.handle, position, 0, count, 0)) {
        ThrowLastFileError(file.file_name.c_str());
    }
}

void FileSetLength(MfcFileCompat& file, unsigned long length) {
    FileSeek(file, static_cast<long>(length), FILE_BEGIN);
    if (!SetEndOfFile(file.handle)) {
        ThrowLastFileError(file.file_name.c_str());
    }
}

unsigned long FileGetLength(MfcFileCompat& file) {
    const unsigned long position = FileGetPosition(file);
    const unsigned long length = FileSeek(file, 0, FILE_END);
    const unsigned long restored = FileSeek(file, static_cast<long>(position),
        FILE_BEGIN);
    if (restored != position) {
        AfxTraceOutput("Warning: CFile position restore mismatch.\n");
    }
    return length;
}

unsigned FileGetBufferPtrUnsupported(unsigned command) {
    if (command != 3U && AfxAssertFailedLine("filecore.cpp", 0x171)) {
        CrtDebugBreak();
    }
    return 0U;
}

void FileRename(const char* old_name, const char* new_name) {
    if (!MoveFileA(old_name, new_name)) {
        ThrowLastFileError(old_name);
    }
}

void FileRemove(const char* path) {
    if (!DeleteFileA(path)) {
        ThrowLastFileError(path);
    }
}

long AfxComCreateInstance(REFCLSID class_id, REFIID interface_id,
    void** object, DWORD class_context, IUnknown* outer) {
    if (object != nullptr) {
        *object = nullptr;
    }
    return CoCreateInstance(class_id, outer, class_context, interface_id, object);
}

long GetClassObject(REFCLSID class_id, REFIID interface_id, void** object) {
    if (object != nullptr) {
        *object = nullptr;
    }
    return CoGetClassObject(class_id, CLSCTX_INPROC_SERVER, nullptr,
        interface_id, object);
}

std::string FormatGuidString(REFGUID guid) {
    char text[64]{};
    std::snprintf(text, sizeof(text),
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

bool QueryInProcServerFromClsid(const char* clsid_text, std::string& server) {
    server.clear();
    if (clsid_text == nullptr) {
        return false;
    }
    HKEY clsid = nullptr;
    if (RegOpenKeyA(HKEY_CLASSES_ROOT, "CLSID", &clsid) != ERROR_SUCCESS) {
        return false;
    }
    HKEY key = nullptr;
    bool ok = false;
    if (RegOpenKeyA(clsid, clsid_text, &key) == ERROR_SUCCESS) {
        HKEY inproc = nullptr;
        if (RegOpenKeyA(key, "InProcServer32", &inproc) == ERROR_SUCCESS) {
            char value[MAX_PATH]{};
            DWORD value_size = sizeof(value);
            DWORD type = 0;
            if (RegQueryValueExA(inproc, nullptr, nullptr, &type,
                    reinterpret_cast<LPBYTE>(value), &value_size) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ)) {
                server = value;
                ok = true;
            }
            RegCloseKey(inproc);
        }
        RegCloseKey(key);
    }
    RegCloseKey(clsid);
    return ok;
}

bool ResolveShellLinkTarget(const char* link_path, char* target,
    unsigned target_chars) {
    if (target != nullptr && target_chars != 0) {
        *target = '\0';
    }
    if (link_path == nullptr || target == nullptr || target_chars == 0) {
        return false;
    }

    IShellLinkA* shell_link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
        CLSCTX_INPROC_SERVER, IID_IShellLinkA,
        reinterpret_cast<void**>(&shell_link));
    if (FAILED(hr) || shell_link == nullptr) {
        return false;
    }
    IPersistFile* persist = nullptr;
    hr = shell_link->QueryInterface(IID_IPersistFile,
        reinterpret_cast<void**>(&persist));
    if (SUCCEEDED(hr) && persist != nullptr) {
        WCHAR wide_path[MAX_PATH]{};
        MultiByteToWideChar(CP_ACP, 0, link_path, -1, wide_path, MAX_PATH);
        hr = persist->Load(wide_path, STGM_READ);
        if (SUCCEEDED(hr)) {
            WIN32_FIND_DATAA data{};
            hr = shell_link->GetPath(target, target_chars, &data, SLGP_UNCPRIORITY);
        }
        persist->Release();
    }
    shell_link->Release();
    return SUCCEEDED(hr);
}

bool AfxFullPath(char* full_path, const char* path) {
    if (full_path == nullptr) {
        return false;
    }
    full_path[0] = '\0';
    if (path == nullptr) {
        return false;
    }
    char* file_part = nullptr;
    DWORD copied = GetFullPathNameA(path, MAX_PATH, full_path, &file_part);
    if (copied == 0 || copied >= MAX_PATH) {
        lstrcpynA(full_path, path, MAX_PATH);
        return false;
    }
    const std::string root = ExtractRootPath(full_path);
    DWORD file_system_flags = 0;
    DWORD max_component = 0;
    if (!root.empty() && GetVolumeInformationA(root.c_str(), nullptr, 0, nullptr,
            &max_component, &file_system_flags, nullptr, 0)) {
        if ((file_system_flags & FS_CASE_IS_PRESERVED) == 0) {
            CharUpperA(full_path);
        }
    }
    return true;
}

std::string ExtractRootPath(const char* path) {
    if (path == nullptr || *path == '\0') {
        return {};
    }
    std::string root = path;
    if (is_dir_sep_char(root[0]) && is_dir_sep_char(root[1])) {
        std::size_t pos = 2;
        pos = root.find_first_of("\\/", pos);
        if (pos != std::string::npos) {
            pos = root.find_first_of("\\/", pos + 1);
            if (pos != std::string::npos) {
                root.resize(pos + 1);
            }
        }
        return root;
    }
    if (root.size() >= 3 && root[1] == ':' && is_dir_sep_char(root[2])) {
        root.resize(3);
        return root;
    }
    const std::size_t slash = root.find_first_of("\\/");
    if (slash != std::string::npos) {
        root.resize(slash + 1);
    }
    return root;
}

bool FileNameCompare(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    return lstrcmpiA(left, right) == 0;
}

int GetFileTitleCompat(const char* path, char* title, unsigned title_chars) {
    if (path == nullptr) {
        return 0;
    }
    char local[MAX_PATH]{};
    char* destination = title == nullptr ? local : title;
    WORD chars = static_cast<WORD>(title == nullptr ? MAX_PATH : title_chars);
    if (GetFileTitleA(path, destination, chars) == 0) {
        return title == nullptr ? lstrlenA(destination) + 1 : 0;
    }
    const char* file_name = path + lstrlenA(path);
    while (file_name > path && !is_dir_sep_char(file_name[-1])) {
        --file_name;
    }
    if (title != nullptr && title_chars != 0) {
        lstrcpynA(title, file_name, static_cast<int>(title_chars));
    }
    return static_cast<int>(std::strlen(file_name) + 1);
}

std::string GetModuleShortFileName(HMODULE module) {
    char long_path[MAX_PATH]{};
    char short_path[MAX_PATH]{};
    if (GetModuleFileNameA(module, long_path, MAX_PATH) == 0) {
        return {};
    }
    if (GetShortPathNameA(long_path, short_path, MAX_PATH) == 0) {
        return long_path;
    }
    return short_path;
}

void FileAssertValid(const MfcFileCompat& file) {
    CObjectAssertValid(&file);
}

MfcFileCompat* DeleteFileScalarDtor(MfcFileCompat* file, unsigned flags) {
    if (file == nullptr) {
        return nullptr;
    }
    if (file->handle != INVALID_HANDLE_VALUE && file->close_on_delete) {
        FileClose(*file);
    }
    file->file_name.clear();
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(file);
    }
    return file;
}

void FileStatusDump(const MfcFileStatusCompat& status) {
    AfxTraceOutput("a CFileStatus at %p\n", &status);
    AfxTraceOutput("m_ctime = ");
    DumpMfcTime(status.creation_time);
    AfxTraceOutput("m_mtime = ");
    DumpMfcTime(status.modified_time);
    AfxTraceOutput("m_atime = ");
    DumpMfcTime(status.access_time);
    AfxTraceOutput("m_size = %lu\n", status.size);
    AfxTraceOutput("m_attribute = 0x%02x\n", status.attribute);
    AfxTraceOutput("m_szFullName = %s\n", status.full_name.data());
}

std::string FileGetFileName(MfcFileCompat& file) {
    MfcFileStatusCompat status{};
    if (!FileGetStatus(file, status)) {
        return {};
    }
    const char* name = status.full_name.data();
    const char* cursor = name + std::strlen(name);
    while (cursor > name && !is_dir_sep_char(cursor[-1])) {
        --cursor;
    }
    return cursor;
}

std::string FileGetFileTitleString(MfcFileCompat& file) {
    MfcFileStatusCompat status{};
    if (!FileGetStatus(file, status)) {
        return {};
    }
    char title[MAX_PATH]{};
    GetFileTitleCompat(status.full_name.data(), title, MAX_PATH);
    return title;
}

std::string FileGetFilePath(MfcFileCompat& file) {
    MfcFileStatusCompat status{};
    if (!FileGetStatus(file, status)) {
        return {};
    }
    return status.full_name.data();
}

bool FileGetStatus(MfcFileCompat& file, MfcFileStatusCompat& status) {
    FileAssertValid(file);
    std::memset(&status, 0, sizeof(status));
    lstrcpynA(status.full_name.data(), file.file_name.c_str(), MAX_PATH);
    if (file.handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    FILETIME creation{};
    FILETIME access{};
    FILETIME write{};
    if (!GetFileTime(file.handle, &creation, &access, &write)) {
        return false;
    }
    DWORD high = 0;
    const DWORD low = GetFileSize(file.handle, &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        return false;
    }
    status.size = low;
    if (!file.file_name.empty()) {
        DWORD attributes = GetFileAttributesA(file.file_name.c_str());
        status.attribute = attributes == INVALID_FILE_ATTRIBUTES
            ? 0 : static_cast<unsigned char>(attributes & 0x7f);
    }
    ConstructMfcTimeFromFileTime(status.creation_time, creation);
    ConstructMfcTimeFromFileTime(status.access_time, access);
    ConstructMfcTimeFromFileTime(status.modified_time, write);
    return true;
}

bool FileGetStatusByPath(const char* path, MfcFileStatusCompat& status) {
    std::memset(&status, 0, sizeof(status));
    if (path == nullptr) {
        return false;
    }
    if (!AfxFullPath(status.full_name.data(), path)) {
        status.full_name[0] = '\0';
        return false;
    }
    WIN32_FIND_DATAA data{};
    HANDLE find = FindFirstFileA(path, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    FindClose(find);
    status.attribute = static_cast<unsigned char>(data.dwFileAttributes & 0x7f);
    status.size = data.nFileSizeLow;
    ConstructMfcTimeFromFileTime(status.creation_time, data.ftCreationTime);
    ConstructMfcTimeFromFileTime(status.access_time, data.ftLastAccessTime);
    ConstructMfcTimeFromFileTime(status.modified_time, data.ftLastWriteTime);
    return true;
}

FILETIME MfcTimeToFileTime(const MfcTimeCompat& time) {
    FILETIME result{};
    if (time.value == 0) {
        return result;
    }
    std::tm* local = std::localtime(&time.value);
    if (local == nullptr) {
        return result;
    }
    SYSTEMTIME system{};
    system.wYear = static_cast<WORD>(local->tm_year + 1900);
    system.wMonth = static_cast<WORD>(local->tm_mon + 1);
    system.wDay = static_cast<WORD>(local->tm_mday);
    system.wHour = static_cast<WORD>(local->tm_hour);
    system.wMinute = static_cast<WORD>(local->tm_min);
    system.wSecond = static_cast<WORD>(local->tm_sec);
    FILETIME local_file{};
    if (!SystemTimeToFileTime(&system, &local_file)) {
        return result;
    }
    LocalFileTimeToFileTime(&local_file, &result);
    return result;
}

void FileSetStatus(const char* path, const MfcFileStatusCompat& status) {
    if (path == nullptr) {
        return;
    }
    DWORD old_attributes = GetFileAttributesA(path);
    if (old_attributes == INVALID_FILE_ATTRIBUTES) {
        ThrowLastFileError(path);
    }
    if ((old_attributes & FILE_ATTRIBUTE_READONLY) != 0 &&
        (status.attribute & FILE_ATTRIBUTE_READONLY) == 0) {
        if (!SetFileAttributesA(path, status.attribute)) {
            ThrowLastFileError(path);
        }
    }
    HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ThrowLastFileError(path);
    }
    FILETIME creation = MfcTimeToFileTime(status.creation_time);
    FILETIME access = MfcTimeToFileTime(status.access_time);
    FILETIME write = MfcTimeToFileTime(status.modified_time);
    if (!SetFileTime(file, &creation, &access, &write)) {
        CloseHandle(file);
        ThrowLastFileError(path);
    }
    if (!CloseHandle(file)) {
        ThrowLastFileError(path);
    }
    if (old_attributes != status.attribute) {
        if (!SetFileAttributesA(path, status.attribute)) {
            ThrowLastFileError(path);
        }
    }
}

bool MemoryFileGetStatus(const MfcFileCompat& file, MfcFileStatusCompat& status) {
    std::memset(&status, 0, sizeof(status));
    status.size = 0;
    status.attribute = 0;
    if (!file.file_name.empty()) {
        lstrcpynA(status.full_name.data(), file.file_name.c_str(), MAX_PATH);
    }
    return true;
}

#endif

} // namespace ranker
