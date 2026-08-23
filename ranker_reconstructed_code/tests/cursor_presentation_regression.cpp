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

    // Mission-briefing portrait/text videos are owned HWNDs above the
    // DirectDraw primary surface.  Their WM_SETCURSOR path must select a
    // native cursor because the software cursor is physically underneath.
    HCURSOR arrow = LoadCursorA(nullptr, IDC_ARROW);
    assert(arrow != nullptr);
    SetCursor(nullptr);
    assert(HandleNativeCursorForOwnedOverlayMessage(WM_SETCURSOR, arrow));
    assert(GetCursor() == arrow);
    assert(!HandleNativeCursorForOwnedOverlayMessage(WM_MOUSEMOVE, arrow));
    assert(HandleNativeCursorForOwnedOverlayMessage(WM_SETCURSOR, nullptr));
    assert(GetCursor() == nullptr);
    SetCursor(nullptr);
#endif
    return 0;
}
