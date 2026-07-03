#include "ranker_bitmap_icon_collection.h"
#include "ranker_crt_runtime.h"

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <sys/utime.h>
#endif

namespace ranker {
namespace {

BitmapIconResourceCollection g_global_bitmap_icons;
bool g_global_bitmap_icons_initialized = false;
constexpr DWORD kLegacyIconPathChars = 0x104;

void shutdown_global_bitmap_icons() {
    DestroyGlobalBitmapIconCollection();
}

std::string default_icon_path(u32 slot) {
    char buffer[kLegacyIconPathChars];
    std::memset(buffer, 0xcc, sizeof(buffer));
    CrtSprintf(buffer, "Icons\\Icon%02u.bmp", slot);
    return buffer;
}

void build_guild_icon_paths_for_slot(u32 slot, std::string& icon_path,
    std::string& directory_path) {
    char current_directory[kLegacyIconPathChars];
    char icon_buffer[kLegacyIconPathChars];
    char directory_buffer[kLegacyIconPathChars];
    std::memset(current_directory, 0xcc, sizeof(current_directory));
    std::memset(icon_buffer, 0xcc, sizeof(icon_buffer));
    std::memset(directory_buffer, 0xcc, sizeof(directory_buffer));
    GetCurrentDirectoryA(kLegacyIconPathChars, current_directory);
    CrtSprintf(icon_buffer, "%s\\Icons\\Guild%03d.bmp", current_directory,
        static_cast<int>(slot));
    CrtSprintf(directory_buffer, "%s\\Icons", current_directory);
    icon_path = icon_buffer;
    directory_path = directory_buffer;
}

bool set_file_modified_time(const std::string& path, std::time_t modified_time) {
#ifdef _WIN32
    struct _utimbuf times {};
    times.actime = modified_time;
    times.modtime = modified_time;
    return _utime(path.c_str(), &times) == 0;
#else
    (void)path;
    (void)modified_time;
    return true;
#endif
}

std::time_t read_file_modified_time(const std::string& path) {
    FILE* file = CrtFopen(path.c_str(), "rb");
    if (file == nullptr) {
        return 0;
    }

    CrtFileStatus status{};
    if (CrtFileStatusByDescriptor(CrtStreamFileDescriptor(file), status) != 0) {
        CrtFclose(file);
        return 0;
    }
    CrtFclose(file);
    return status.modify_time;
}

} // namespace

BitmapIconResourceCollection& GlobalBitmapIconResourceCollection() {
    if (!g_global_bitmap_icons_initialized) {
        InitializeGlobalBitmapIconCollection();
        RegisterGlobalBitmapIconCollectionDestructor();
    }
    return g_global_bitmap_icons;
}

void InitializeGlobalBitmapIconCollectionStatic() {
    InitializeGlobalBitmapIconCollection();
    RegisterGlobalBitmapIconCollectionDestructor();
}

void InitializeGlobalBitmapIconCollection() {
    InitializeBitmapIconResourceCollection(g_global_bitmap_icons);
    g_global_bitmap_icons_initialized = true;
}

void RegisterGlobalBitmapIconCollectionDestructor() {
    std::atexit(shutdown_global_bitmap_icons);
}

void DestroyGlobalBitmapIconCollection() {
    HandleBitmapIconResourceCollectionDestructor(g_global_bitmap_icons);
    g_global_bitmap_icons_initialized = false;
}

void InitializeBitmapIconResourceCollection(BitmapIconResourceCollection& icons) {
    for (BitmapMemoryResource& slot : icons.slots) {
        InitializeBitmapMemoryResource(slot);
    }
    icons.loaded_count = 0;
    icons.loaded.fill(false);
    icons.guild_loaded_count = 0;
    icons.guild_timestamps.fill(0);
}

void HandleBitmapIconResourceCollectionDestructor(BitmapIconResourceCollection& icons) {
    ReleaseAllBitmapIconSlots(icons);
    for (BitmapMemoryResource& slot : icons.slots) {
        HandleBitmapMemoryResourceDestructor(slot);
    }
}

void ReleaseAllBitmapIconSlots(BitmapIconResourceCollection& icons) {
    for (u32 slot = 0; slot < kBitmapIconSlotCount; ++slot) {
        ReleaseBitmapIconSlot(icons, slot);
    }
    icons.loaded_count = 0;
    icons.guild_loaded_count = 0;
    icons.guild_timestamps.fill(0);
}

bool LoadBitmapIconSlotFromJw219Trc(BitmapIconResourceCollection& icons, u32 slot,
    u32 record_index) {
    if (slot >= kBitmapIconSlotCount) {
        return false;
    }

    if (icons.loaded[slot]) {
        ReleaseBitmapIconSlot(icons, slot);
    }

    if (!LoadBitmapMemoryResourceFromTrcRecord(icons.slots[slot], "Jw2_19.trc",
            record_index)) {
        return false;
    }

    icons.loaded[slot] = true;
    ++icons.loaded_count;
    return true;
}

bool LoadBitmapIconSlotFromFile(BitmapIconResourceCollection& icons, u32 slot,
    const char* path) {
    if (slot >= kBitmapIconSlotCount) {
        return false;
    }

    if (icons.loaded[slot]) {
        ReleaseBitmapIconSlot(icons, slot);
    }

    if (!LoadBitmapMemoryResourceFromFile(icons.slots[slot], path)) {
        return false;
    }

    icons.loaded[slot] = true;
    ++icons.loaded_count;
    return true;
}

void ReleaseBitmapIconSlot(BitmapIconResourceCollection& icons, u32 slot) {
    if (slot >= kBitmapIconSlotCount || !icons.loaded[slot]) {
        return;
    }

    ReleaseBitmapMemoryResource(icons.slots[slot]);
    icons.loaded[slot] = false;
    --icons.loaded_count;
}

void LoadDefaultIconBitmapSet(BitmapIconResourceCollection& icons) {
    ReleaseAllBitmapIconSlots(icons);

    for (u32 slot = 0; slot < kGuildBitmapIconSlotBase; ++slot) {
        const std::string path = default_icon_path(slot);
        LoadBitmapIconSlotFromFile(icons, slot, path.c_str());
    }

    for (u32 guild = 0; guild < kGuildBitmapIconSlotCount; ++guild) {
        LoadGuildIconBitmapSlot(icons, guild);
    }
}

void WriteGuildIconBitmapFile(u32 guild_index, const void* data, std::size_t byte_count,
    std::time_t modified_time) {
    const u32 slot = guild_index + kGuildBitmapIconSlotBase;
    if (slot >= kBitmapIconSlotCount) {
        return;
    }

    std::string path;
    std::string directory;
    build_guild_icon_paths_for_slot(slot, path, directory);

    CreateDirectoryA(directory.c_str(), nullptr);
    DeleteFileA(path.c_str());

    FILE* file = CrtFopen(path.c_str(), "wb");
    CrtFwrite(data, byte_count, 1, file);
    CrtFclose(file);

    set_file_modified_time(path, modified_time);
}

bool LoadGuildIconBitmapSlot(BitmapIconResourceCollection& icons, u32 guild_index) {
    const u32 slot = guild_index + kGuildBitmapIconSlotBase;
    if (slot >= kBitmapIconSlotCount) {
        return false;
    }

    std::string path;
    std::string directory;
    build_guild_icon_paths_for_slot(slot, path, directory);
    if (!LoadBitmapIconSlotFromFile(icons, slot, path.c_str())) {
        return false;
    }

    icons.guild_timestamps[guild_index] = read_file_modified_time(path);
    ++icons.guild_loaded_count;
    return true;
}

void ReleaseGuildIconBitmapSlot(BitmapIconResourceCollection& icons, u32 guild_index) {
    const u32 slot = guild_index + kGuildBitmapIconSlotBase;
    if (slot >= kBitmapIconSlotCount || !icons.loaded[slot]) {
        return;
    }

    ReleaseBitmapIconSlot(icons, slot);
    --icons.guild_loaded_count;
}

BitmapMemoryResource& GetBitmapIconSlotOrDefault(BitmapIconResourceCollection& icons,
    u32 slot) {
    if (slot < kBitmapIconSlotCount && icons.loaded[slot]) {
        return icons.slots[slot];
    }
    return icons.slots[kBitmapIconDefaultSlot];
}

const BitmapMemoryResource& GetBitmapIconSlotOrDefault(
    const BitmapIconResourceCollection& icons, u32 slot) {
    if (slot < kBitmapIconSlotCount && icons.loaded[slot]) {
        return icons.slots[slot];
    }
    return icons.slots[kBitmapIconDefaultSlot];
}

}
