#include "ranker_setup_data.h"

#ifdef _WIN32

#include "ranker_legacy_file.h"

#include <algorithm>
#include <cstring>

namespace ranker {
namespace {

std::array<u8, kSetupDataBytes> g_setup_data{};
bool g_setup_data_loaded = false;
SetupDataWriteFailureCallback g_write_failure_callback = nullptr;
void* g_write_failure_user_data = nullptr;

bool setup_range_available(std::size_t offset, std::size_t bytes) {
    return offset <= g_setup_data.size() && bytes <= g_setup_data.size() - offset;
}

} // namespace

void SetDefaultSetupDataWriteFailureCallback(
    SetupDataWriteFailureCallback callback, void* user_data) {
    g_write_failure_callback = callback;
    g_write_failure_user_data = user_data;
}

void InitializeDefaultSetupDataBuffer() {
    g_setup_data.fill(0);
    std::memcpy(g_setup_data.data(), kSetupSignatureText,
        std::min<std::size_t>(kSetupSignatureBytes, g_setup_data.size()));
}

bool LoadDefaultSetupDataBuffer() {
    if (g_setup_data_loaded) {
        return true;
    }

    if (!HandleSetupDataConfiguredRead(g_setup_data.data(),
            static_cast<LONG>(g_setup_data.size()))) {
        InitializeDefaultSetupDataBuffer();
        g_setup_data_loaded = true;
        return false;
    }

    g_setup_data_loaded = true;
    return true;
}

bool WriteDefaultSetupDataBuffer() {
    LoadDefaultSetupDataBuffer();
    const bool written = HandleSetupDataConfiguredWrite(g_setup_data.data(),
        static_cast<LONG>(g_setup_data.size()));
    g_setup_data_loaded = true;
    if (!written && g_write_failure_callback != nullptr) {
        g_write_failure_callback("_SETUP.DAT", g_write_failure_user_data);
    }
    return written;
}

u32 ImportSetupU32(std::size_t offset, u32 default_value) {
    if (!LoadDefaultSetupDataBuffer() || !setup_range_available(offset, sizeof(u32))) {
        return default_value;
    }
    const u8* bytes = g_setup_data.data() + offset;
    return static_cast<u32>(bytes[0]) |
        (static_cast<u32>(bytes[1]) << 8) |
        (static_cast<u32>(bytes[2]) << 16) |
        (static_cast<u32>(bytes[3]) << 24);
}

i32 ImportSetupI32(std::size_t offset, i32 default_value) {
    const u32 default_bits = static_cast<u32>(default_value);
    const u32 raw = ImportSetupU32(offset, default_bits);
    return WrappedU32ToI32(raw);
}

void ExportSetupU32(std::size_t offset, u32 value) {
    LoadDefaultSetupDataBuffer();
    if (!setup_range_available(offset, sizeof(u32))) {
        return;
    }
    u8* bytes = g_setup_data.data() + offset;
    bytes[0] = static_cast<u8>(value & 0xffu);
    bytes[1] = static_cast<u8>((value >> 8) & 0xffu);
    bytes[2] = static_cast<u8>((value >> 16) & 0xffu);
    bytes[3] = static_cast<u8>((value >> 24) & 0xffu);
}

void ExportSetupI32(std::size_t offset, i32 value) {
    u32 raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    ExportSetupU32(offset, raw);
}

void ImportSetupText(char* target, std::size_t target_size, std::size_t offset) {
    if (target == nullptr || target_size == 0) {
        return;
    }
    LoadDefaultSetupDataBuffer();
    std::memset(target, 0, target_size);
    if (offset >= g_setup_data.size()) {
        return;
    }
    const std::size_t bytes = std::min<std::size_t>(
        target_size - 1, g_setup_data.size() - offset);
    std::memcpy(target, g_setup_data.data() + offset, bytes);
}

void ExportSetupText(std::size_t offset, const char* source,
    std::size_t source_size) {
    if (source == nullptr || source_size == 0) {
        return;
    }
    LoadDefaultSetupDataBuffer();
    if (offset >= g_setup_data.size()) {
        return;
    }
    const std::size_t bytes = std::min<std::size_t>(
        source_size, g_setup_data.size() - offset);
    std::memset(g_setup_data.data() + offset, 0, bytes);
    std::memcpy(g_setup_data.data() + offset, source, bytes);
}

} // namespace ranker

#endif
