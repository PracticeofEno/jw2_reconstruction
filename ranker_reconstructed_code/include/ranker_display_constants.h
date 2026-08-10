#pragma once

namespace ranker {

// Ranker_WinMain initializes DAT_0143fff0/01440004/0143ffec to 800x600x16.
// These are logical/back-buffer dimensions.  Keep them independent from the
// reconstructed window's presentation size so UI/gameplay coordinates and P2P
// simulation behavior continue to match the original executable.
constexpr int kOriginalClientWidth = 800;
constexpr int kOriginalClientHeight = 600;
constexpr int kOriginalColorDepth = 16;

// ranker_rebuild presents the original 4:3 surface in a larger native client
// area.  ddraw.ini may override these values, but a missing or malformed INI
// must still start at the reconstructed client's supported default size.
constexpr int kDefaultPresentationClientWidth = 1280;
constexpr int kDefaultPresentationClientHeight = 960;

static_assert(kDefaultPresentationClientWidth * kOriginalClientHeight ==
    kDefaultPresentationClientHeight * kOriginalClientWidth);

} // namespace ranker
