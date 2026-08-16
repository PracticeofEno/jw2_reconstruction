#include "ranker_gameplay_cheats.h"
#include "ranker_gameplay_session_flow.h"
#include "ranker_gameplay_session_rules.h"

#include <cassert>

int main() {
    using namespace ranker;

    assert(GameplayCheatTextChecksum("abc") ==
        ((static_cast<u32>('a') ^ 3u) +
         (static_cast<u32>('b') ^ 2u) +
         (static_cast<u32>('c') ^ 1u)));

    GameplayCheatMatch match = ResolveLocalGameplayCheatSignature(
        16u, 0x0b59u, false, false);
    assert(match.recognized && match.command == 0u && !match.uses_selected_unit);
    match = ResolveLocalGameplayCheatSignature(13u, 0x0539u, false, false);
    assert(match.recognized && match.command == 0u);
    match = ResolveLocalGameplayCheatSignature(14u, 0x04e5u, false, false);
    assert(match.recognized && match.command == 2u);
    assert(!ResolveLocalGameplayCheatSignature(
        14u, 0x04e5u, true, false).recognized);
    assert(!ResolveLocalGameplayCheatSignature(
        10u, 0x06d9u, false, false).recognized);
    match = ResolveLocalGameplayCheatSignature(10u, 0x06d9u, false, true);
    assert(match.recognized && match.command == 16u && match.uses_selected_unit);
    assert(!ResolveLocalGameplayCheatSignature(3u, 123u, false, true).recognized);

    assert(ResolveGameplayModalSessionRequest(true, true) ==
        GameplayModalSessionRequest::Restart);
    assert(ResolveGameplayModalSessionRequest(false, true) ==
        GameplayModalSessionRequest::Leave);
    assert(ResolveGameplayModalSessionRequest(false, false) ==
        GameplayModalSessionRequest::None);

    // Scenario Restart publishes the modal's end-session bit, while Quit to
    // Frontend publishes its distinct leave bit.  Restart must win even if a
    // stale leave request is also present.
    assert(ResolveGameplayModalSessionRequest(true, false) ==
        GameplayModalSessionRequest::Restart);

    assert(ResolveGameplayInitialSpeedIndex(false, false, 8u) == 0u);
    assert(ResolveGameplayInitialSpeedIndex(true, false, 8u) == 8u);
    assert(ResolveGameplayInitialSpeedIndex(false, true, 8u) == 8u);

    // A frontend-loaded save must preserve its serialized unit pool just like
    // an in-game load.  Only a genuinely fresh skirmish keeps the map mode.
    assert(ResolveGameplayMaterializationMode(0u, true, false, false) == 5u);
    assert(ResolveGameplayMaterializationMode(2u, false, true, false) == 5u);
    assert(ResolveGameplayMaterializationMode(1u, false, false, true) == 5u);
    assert(ResolveGameplayMaterializationMode(3u, false, false, false) == 3u);

    assert(ResolveGameplayManualLeaveResult(0u, false) == 2u);
    assert(ResolveGameplayManualLeaveResult(0x707u, false) == 2u);
    assert(ResolveGameplayManualLeaveResult(0x708u, false) == 1u);
    assert(ResolveGameplayManualLeaveResult(0x708u, true) == 2u);
    assert(!ResolveGameplayPauseOverlayVisible(false));
    assert(ResolveGameplayPauseOverlayVisible(true));

    // Generic/P2P "Quit Program" exits the current match flow; only the
    // local worker shutdown edge closes the application window.
    assert(!ShouldCloseApplicationAfterP2PMatch(false, true));
    assert(ShouldCloseApplicationAfterP2PMatch(true, false));
    return 0;
}
