#include "ranker_winmain.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "DIRECTDRAW_PRESENTATION_MODE_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace ranker;

    // Ranker_WinMain (0x004073f4..0x00407400) publishes an 800x600x16
    // logical surface. The original 32-bit executable is presented at the
    // independent cnc-ddraw client size (1280x960 in the local ddraw.ini).
    // ranker_rebuild is 64-bit, so it must apply the same coordinate mapping
    // before dispatching its client mouse messages.
    require(kOriginalClientWidth == 800,
        "logical width no longer matches DAT_0143fff0");
    require(kOriginalClientHeight == 600,
        "logical height no longer matches DAT_01440004");
    require(kOriginalColorDepth == 16,
        "logical color depth no longer matches DAT_0143ffec");
    require(ScalePresentationCoordinateToLogical(0, 1280, 800) == 0,
        "left edge did not stay at logical zero");
    require(ScalePresentationCoordinateToLogical(640, 1280, 800) == 400,
        "horizontal midpoint did not map to the logical midpoint");
    require(ScalePresentationCoordinateToLogical(1279, 1280, 800) == 799,
        "right client pixel did not reach the logical edge");
    require(ScalePresentationCoordinateToLogical(1280, 1280, 800) == 799,
        "captured pointer beyond the client was not clamped");
    require(ScalePresentationCoordinateToLogical(-1, 1280, 800) == 0,
        "negative captured pointer was not clamped");
    require(ScalePresentationCoordinateToLogical(959, 960, 600) == 599,
        "bottom client pixel did not reach the logical edge");
    require(ScalePresentationCoordinateToLogical(317, 800, 800) == 317,
        "800-wide fallback client did not preserve identity mapping");

    std::cout <<
        "DIRECTDRAW_PRESENTATION_MODE_PASS logical=800x600x16 "
        "client=1280x960 mapping=clamped-stretch\n";
    return EXIT_SUCCESS;
}
