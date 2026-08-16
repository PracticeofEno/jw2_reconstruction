#pragma once

#include "ranker_runtime_resources.h"

#include <array>
#include <cstddef>

namespace ranker {

// FUN_00409390/FUN_00409720 replace group 3 (definition +0x2220), whose
// frames are the unit death animation selected by the Change Death option.
constexpr u32 kUnitDeathAnimationImageGroup = 3;

std::array<bool, kUnitDefinitionResourceCount> ParseUnitDeathResourceManifest(
    const u8* bytes, std::size_t byte_count);

}
