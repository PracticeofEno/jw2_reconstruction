#include "ranker_gameplay_modal_rules.h"

#include <cassert>

int main() {
    using namespace ranker;

    assert(GameplayPauseMenuSaveLoadEnabled(false));
    assert(!GameplayPauseMenuSaveLoadEnabled(true));
    assert(GameplayMenuEntryFlagsForEnabledState(0x1004u, false) == 0x1004u);
    assert(GameplayMenuEntryFlagsForEnabledState(0x1004u, true) == 0x1004u);
    assert(GameplayPauseMenuUsesChildSnapshot(false));
    assert(!GameplayPauseMenuUsesChildSnapshot(true));

    assert(!GameplayPauseMenuModalPauseEnabled(false, false, false, 4));
    assert(!GameplayPauseMenuModalPauseEnabled(true, true, false, 4));
    assert(!GameplayPauseMenuModalPauseEnabled(false, true, false, 0));
    assert(GameplayPauseMenuModalPauseEnabled(false, true, false, 1));
    assert(GameplayPauseMenuModalPauseEnabled(false, true, true, 0));

    assert(!ShouldRestoreIdleUiScreenEntryState(-1, false));
    assert(!ShouldRestoreIdleUiScreenEntryState(1, false));
    assert(!ShouldRestoreIdleUiScreenEntryState(2, true));
    assert(ShouldRestoreIdleUiScreenEntryState(2, false));
    assert(ShouldRestoreIdleUiScreenEntryState(3, false));
    return 0;
}
