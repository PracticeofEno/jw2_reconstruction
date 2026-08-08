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

} // namespace ranker
