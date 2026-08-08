#pragma once

#include "ranker_types.h"

#include <vector>

namespace ranker {

// Renders a deterministic lobby preview without touching gameplay RNG state.
bool RenderGameplaySessionMinimapPreview(const char* archive_path,
    u32 preview_width, u32 preview_height, std::vector<u16>& pixels);

} // namespace ranker
