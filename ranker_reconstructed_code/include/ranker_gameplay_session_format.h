#pragma once

#include "ranker_types.h"

#include <cstddef>

namespace ranker {

// Session/map TRC record layout shared by the live importer and read-only
// lobby preview renderer.
constexpr u32 kGameplaySessionMapRecordIndex = 1;
constexpr u32 kGameplayMapSourceLayerRecordIndex = 10;
constexpr u32 kGameplayMapOverlayLayerRecordIndex = 11;
constexpr u32 kGameplayScenarioMapLayerStrideTiles = 0x100;

constexpr std::size_t kSessionMapRecordTerrainBankOffset = 0x160;
constexpr std::size_t kSessionMapRecordWidthTilesOffset = 0x164;
constexpr std::size_t kSessionMapRecordHeightTilesOffset = 0x168;

// HandleGameplaySessionBundleImport copies the primary JW2 record to the live
// 0x00705d98 runtime block.  The camera globals 0x007071a8/0x007071ac are
// therefore stored at these offsets and mode-5 sessions retain them unless a
// local observer explicitly recenters on the map.
constexpr std::size_t kSessionPrimaryCameraXOffset = 0x1410;
constexpr std::size_t kSessionPrimaryCameraYOffset = 0x1414;

struct GameplaySessionPrimaryCamera {
    i32 x = 0;
    i32 y = 0;
};

inline bool ReadGameplaySessionPrimaryCamera(const u8* record,
    std::size_t record_size, GameplaySessionPrimaryCamera& camera) {
    if (record == nullptr ||
        record_size < kSessionPrimaryCameraYOffset + sizeof(u32)) {
        return false;
    }

    const auto read_i32 = [record](std::size_t offset) {
        const u32 value = static_cast<u32>(record[offset]) |
            (static_cast<u32>(record[offset + 1]) << 8) |
            (static_cast<u32>(record[offset + 2]) << 16) |
            (static_cast<u32>(record[offset + 3]) << 24);
        return static_cast<i32>(value);
    };
    camera.x = read_i32(kSessionPrimaryCameraXOffset);
    camera.y = read_i32(kSessionPrimaryCameraYOffset);
    return true;
}

} // namespace ranker
