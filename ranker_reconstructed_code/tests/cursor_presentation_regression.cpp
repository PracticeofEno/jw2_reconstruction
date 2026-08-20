#include "ranker_cursor.h"

#include <cassert>

int main() {
#ifdef _WIN32
    using ranker::ShouldPresentGameCursorImmediatelyForPointerMotion;

    // Timer-driven frontend screens still need a cursor-only update between
    // their 33 ms redraws.
    assert(ShouldPresentGameCursorImmediatelyForPointerMotion(true, false));

    // While the gameplay worker owns the peer-startup warm-up presentations,
    // WM_MOUSEMOVE must not issue a second synchronous D3D9 Present.
    assert(!ShouldPresentGameCursorImmediatelyForPointerMotion(true, true));

    // The legacy DirectDraw cursor path updates its primary surface directly.
    assert(!ShouldPresentGameCursorImmediatelyForPointerMotion(false, false));
    assert(!ShouldPresentGameCursorImmediatelyForPointerMotion(false, true));
#endif
    return 0;
}
