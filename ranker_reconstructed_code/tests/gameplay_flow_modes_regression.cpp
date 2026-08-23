#include "ranker_gameplay_cheats.h"
#include "ranker_directx.h"
#include "ranker_gameplay_frame_render.h"
#include "ranker_gameplay_packets.h"
#include "ranker_player_slots.h"
#include "ranker_gameplay_script.h"
#include "ranker_gameplay_sound.h"
#include "ranker_gameplay_session_format.h"
#include "ranker_gameplay_session_flow.h"
#include "ranker_gameplay_session_rules.h"
#include "ranker_gameplay_visibility.h"
#include "ranker_frontend_stage_flow.h"
#include "ranker_miles.h"
#include "ranker_ui_screen.h"

#include <array>
#include <cassert>

int main() {
    using namespace ranker;

    // Cancelling a campaign briefing returns to the silent stage-selection
    // frontend. Original FUN_004d94c7 applies policy 1 before opening it.
    static_assert(kFrontendStageSelectionMusicPolicyMode == 1u);
    static_assert(ShouldRestoreFrontendStageSelectionMusic(false));
    static_assert(!ShouldRestoreFrontendStageSelectionMusic(true));
    static_assert(PrimaryMilesBriefingMusicRecordForFaction(0u) == 0x0eu);
    static_assert(PrimaryMilesBriefingMusicRecordForFaction(3u) == 0x11u);
    static_assert(Jw204BinkMenuPreloadMatches(true, 0u, 0, 0));
    static_assert(Jw204BinkMenuPreloadMatches(true, 620u, 3, 7));
    static_assert(!Jw204BinkMenuPreloadMatches(false, 0u, 0, 0));
    static_assert(!Jw204BinkMenuPreloadMatches(true, 20u, 0, 0));

    // Script opcode 0x01 is an immediate draw in the original dispatcher. It
    // must use the frame slot, never the five-second notification slot. An
    // inactive dialog must also leave another immediate script draw intact.
    GameplayHudTextState script_hud{};
    const char script_text[] = "active dialog";
    PublishGameplayFrameMessage(
        script_hud, true, script_text, 100, 300, 1234);
    assert(script_hud.current_message.text == nullptr);
    assert(script_hud.frame_message.text == script_text);
    assert(script_hud.frame_message.x == 100);
    assert(script_hud.frame_message.y == 300);
    assert(script_hud.frame_message.tick_ms == 1234);
    PublishGameplayFrameMessage(
        script_hud, false, script_text, 100, 300, 1250);
    assert(script_hud.frame_message.text == script_text);

    std::array<u8, kSessionPrimaryCameraYOffset + sizeof(u32)>
        primary_record{};
    const auto write_u32 = [&primary_record](std::size_t offset, u32 value) {
        primary_record[offset] = static_cast<u8>(value);
        primary_record[offset + 1] = static_cast<u8>(value >> 8);
        primary_record[offset + 2] = static_cast<u8>(value >> 16);
        primary_record[offset + 3] = static_cast<u8>(value >> 24);
    };
    write_u32(kSessionPrimaryCameraXOffset, 1386u);
    write_u32(kSessionPrimaryCameraYOffset, 418u);
    GameplaySessionPrimaryCamera primary_camera{};
    assert(ReadGameplaySessionPrimaryCamera(primary_record.data(),
        primary_record.size(), primary_camera));
    assert(primary_camera.x == 1386);
    assert(primary_camera.y == 418);
    assert(!ReadGameplaySessionPrimaryCamera(primary_record.data(),
        kSessionPrimaryCameraYOffset + sizeof(u32) - 1, primary_camera));

    static_assert(NormalizePlayerOrNoLocalSlot(0) == 0);
    static_assert(NormalizePlayerOrNoLocalSlot(7) == 7);
    static_assert(NormalizePlayerOrNoLocalSlot(9) == 9);
    static_assert(NormalizePlayerOrNoLocalSlot(8) == kNoLocalPlayerSlot);
    static_assert(NormalizePlayerOrNoLocalSlot(10) == kNoLocalPlayerSlot);

    PlayerSlotRuntimeState replay_observer{};
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        replay_observer.owner_relation_masks[owner] = 1u << owner;
        replay_observer.owner_visibility_masks[owner] = 1u << owner;
    }
    ApplyReplayNoLocalPlayerVisibility(replay_observer);
    for (u32 owner = 0; owner < kPlayerSlotCount; ++owner) {
        assert(replay_observer.owner_relation_masks[owner] == (1u << owner));
        assert(replay_observer.owner_visibility_masks[owner] ==
            ((1u << owner) | (1u << kNoLocalPlayerSlot)));
    }
    assert(ResolveLocalOwnerRelationMask(replay_observer, 0) == 1u);
    assert(ResolveLocalOwnerRelationMask(
        replay_observer, kNoLocalPlayerSlot) == 0xffffffffu);
    assert(ResolveLocalOwnerRelationMask(replay_observer, 10) == 0u);

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

    // FUN_004e7b68's complete 34-entry table contains 25 usable signatures
    // and nine 0xffff sentinels.  Verify both Korean/alternate groups map to
    // the same nested subtype-0x0d commands.
    constexpr std::array<u32, 34> kExpectedCommands{{
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
        kInvalidGameplayCheatCommand, kInvalidGameplayCheatCommand,
        kInvalidGameplayCheatCommand, kInvalidGameplayCheatCommand,
        14u, 15u, 16u, 17u, 18u,
        kInvalidGameplayCheatCommand, kInvalidGameplayCheatCommand,
        kInvalidGameplayCheatCommand, kInvalidGameplayCheatCommand,
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
        kInvalidGameplayCheatCommand,
    }};
    for (u32 index = 0; index < kGameplayCheatSignatures.size(); ++index) {
        const GameplayCheatSignature signature = kGameplayCheatSignatures[index];
        const GameplayCheatMatch table_match =
            ResolveLocalGameplayCheatSignature(
                signature.byte_count, signature.checksum, false, true);
        if (kExpectedCommands[index] == kInvalidGameplayCheatCommand) {
            assert(!table_match.recognized);
        }
        else {
            assert(table_match.recognized);
            assert(table_match.command == kExpectedCommands[index]);
            assert(table_match.uses_selected_unit ==
                (kExpectedCommands[index] == 16u));
            const GameplayCheatMatch restricted_match =
                ResolveLocalGameplayCheatSignature(
                    signature.byte_count, signature.checksum, true, true);
            if (2u <= kExpectedCommands[index] &&
                kExpectedCommands[index] <= 14u) {
                assert(!restricted_match.recognized);
            }
            else {
                assert(restricted_match.recognized);
            }
        }
    }
    assert(!ResolveLocalGameplayCheatSignature(
        0xffffu, 0xffffu, false, true).recognized);

    for (u32 command = 0; command <= 18u; ++command) {
        const GameplayCheatTransitionRequest transition =
            ResolveGameplayCheatTransitionRequest(command);
        const bool expected = command == 0u ||
            (2u <= command && command <= 14u);
        assert(transition.requested == expected);
        if (command == 0u) {
            assert(transition.local_scene_change);
            assert(!transition.write_transition_index);
        }
        else if (expected) {
            assert(!transition.local_scene_change);
            assert(transition.write_transition_index);
        }
    }
    assert(ResolveGameplayCheatTransitionRequest(2u).transition_index == -1);
    for (u32 command = 3u; command <= 12u; ++command) {
        assert(ResolveGameplayCheatTransitionRequest(command).transition_index ==
            static_cast<i32>(command - 3u));
    }
    assert(ResolveGameplayCheatTransitionRequest(13u).transition_index == 10);
    assert(ResolveGameplayCheatTransitionRequest(14u).transition_index == 7);
    assert(ApplyGameplayCheatPrimaryResourceBonus(250u) == 10250u);

    // CP949 "내 친구의 집은 어디인가?" is signature/command 1.  Its first
    // toggle disables fog in both the world and minimap; the second restores
    // the normal visibility gate after promoting every tile as explored.
    match = ResolveLocalGameplayCheatSignature(24u, 0x1006u, false, false);
    assert(match.recognized && match.command == 1u);
    Mode1GameplayPacketDispatchState packet_state{};
    assert(packet_state.local_toggle_state);
    packet_state.local_toggle_state = !packet_state.local_toggle_state;
    assert(!packet_state.local_toggle_state);
    assert(GameplayFogRevealDisabledForCheatToggle(false));
    assert(GameplayVisibilityGateForFogCheatToggle(false) == 0u);
    assert(!GameplayFogRevealDisabledForCheatToggle(true));
    assert(GameplayVisibilityGateForFogCheatToggle(true) == 1u);
    assert(!ShouldRenderGameplayFogOverlay(true));
    assert(ShouldRenderGameplayFogOverlay(false));

    assert(ResolveGameplayModalSessionRequest(true, true) ==
        GameplayModalSessionRequest::Restart);
    assert(ResolveGameplayModalSessionRequest(false, true) ==
        GameplayModalSessionRequest::Leave);
    assert(ResolveGameplayModalSessionRequest(false, false) ==
        GameplayModalSessionRequest::None);

    // Replay EOF is the common session-leave edge, not a gameplay outcome.
    // Only an active playback may consume the replay pump's completion flag.
    assert(ShouldRouteReplayCompletionToSessionLeave(true, true));
    assert(!ShouldRouteReplayCompletionToSessionLeave(true, false));
    assert(!ShouldRouteReplayCompletionToSessionLeave(false, true));

    assert(ResolveGameplayRestartMaterialization(false, true, false) ==
        GameplayRestartMaterialization::NetworkAiPractice);
    assert(ResolveGameplayRestartMaterialization(true, true, true) ==
        GameplayRestartMaterialization::LoadedSession);
    assert(ResolveGameplayRestartMaterialization(false, false, true) ==
        GameplayRestartMaterialization::FrontendStage);
    assert(ResolveGameplayRestartMaterialization(false, false, false) ==
        GameplayRestartMaterialization::Unavailable);

    assert(ShouldAdoptFrontendBootstrapSoundBank(false, true));
    assert(!ShouldAdoptFrontendBootstrapSoundBank(true, true));
    assert(!ShouldAdoptFrontendBootstrapSoundBank(false, false));

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

    assert(ResolveJw204BriefingActivation(0u) ==
        Jw204BriefingActivation::Continue);
    assert(ResolveJw204BriefingActivation(1u) ==
        Jw204BriefingActivation::StartMission);
    assert(ResolveJw204BriefingActivation(2u) ==
        Jw204BriefingActivation::ReplayBriefing);
    assert(ResolveJw204BriefingActivation(3u) ==
        Jw204BriefingActivation::Cancel);
    assert(ResolveJw204BriefingActivation(9u) ==
        Jw204BriefingActivation::Continue);

#ifdef _WIN32
    // Campaign story cards retain the original wait cursor and may only be
    // dismissed with Escape. Mouse buttons and other keys remain ignored.
    static_assert(ShouldHoldSingleFrameBinkUntilCancelled(1u));
    static_assert(!ShouldHoldSingleFrameBinkUntilCancelled(0u));
    static_assert(!ShouldHoldSingleFrameBinkUntilCancelled(2u));
    static_assert(ShouldDeferBinkVideoWindowRelease(
        BinkVideoSkipInputPolicy::EscapeOnly, 1u, true, true));
    static_assert(!ShouldDeferBinkVideoWindowRelease(
        BinkVideoSkipInputPolicy::EscapeOnly, 1u, false, true));
    static_assert(!ShouldDeferBinkVideoWindowRelease(
        BinkVideoSkipInputPolicy::AnyKeyOrMouse, 1u, true, true));
    static_assert(!ShouldDeferBinkVideoWindowRelease(
        BinkVideoSkipInputPolicy::EscapeOnly, 2u, true, true));
    assert(ShouldCancelBinkVideoForWindowInput(
        BinkVideoSkipInputPolicy::EscapeOnly, WM_KEYDOWN, VK_ESCAPE));
    assert(!ShouldCancelBinkVideoForWindowInput(
        BinkVideoSkipInputPolicy::EscapeOnly, WM_KEYDOWN, VK_RETURN));
    assert(!ShouldCancelBinkVideoForWindowInput(
        BinkVideoSkipInputPolicy::EscapeOnly, WM_LBUTTONDOWN, 0));
    assert(ShouldCancelBinkVideoForWindowInput(
        BinkVideoSkipInputPolicy::AnyKeyOrMouse, WM_LBUTTONDOWN, 0));
#endif

    // A two-human UMS lobby must retain the archive-authored Computer owners.
    // Super Elf uses owner 4 in trigger group 62; disabling that slot makes
    // the group appear dead and opens the victory modal at frame 67.
    assert(ResolveGameplayStartupActiveSlotCount(8u, 2u, 5u) == 8u);
    assert(ResolveGameplayStartupActiveSlotCount(8u, 2u, 0u) == 2u);
    assert(ResolveGameplayStartupSlotState(1u, 0x14u, 4u, 2u, 5u) == 1u);
    assert(ResolveGameplayStartupSlotState(0u, 0x14u, 6u, 2u, 5u) == 0x14u);
    assert(ResolveGameplayStartupSlotState(1u, 0x14u, 4u, 2u, 0u) == 0x14u);

    // Super Elf retains inactive fixed-pool nodes in groups 50..63.  Original
    // FUN_00416440 clears a group slot only for raw +0xa0 bit 4, not merely
    // because the node was moved off the active list during UMS startup.
    assert(!ShouldRemoveGameplayScriptGroupReference(0u));
    assert(!ShouldRemoveGameplayScriptGroupReference(1u));
    assert(ShouldRemoveGameplayScriptGroupReference(4u));
    assert(ShouldRemoveGameplayScriptGroupReference(5u));

    assert(ResolveGameplayManualLeaveResult(0u, false) == 2u);
    assert(ResolveGameplayManualLeaveResult(0x707u, false) == 2u);
    assert(ResolveGameplayManualLeaveResult(0x708u, false) == 1u);
    assert(ResolveGameplayManualLeaveResult(0x708u, true) == 2u);
    assert(!ShouldUseGameplaySurrenderEntry(true, 0x707u, 3u));
    assert(ShouldUseGameplaySurrenderEntry(true, 0x708u, 3u));
    assert(!ShouldUseGameplaySurrenderEntry(true, 0x708u, 2u));
    assert(!ShouldUseGameplaySurrenderEntry(false, 0x708u, 3u));
    assert(!ResolveGameplayPauseOverlayVisible(false));
    assert(ResolveGameplayPauseOverlayVisible(true));
    static_assert(!ShouldSynthesizeTransportPlayerDeparture(
        static_cast<u8>(PlayerSlotState::disabled)));
    static_assert(ShouldSynthesizeTransportPlayerDeparture(
        static_cast<u8>(PlayerSlotState::active)));
    assert(!Mode1GameplayPacketDispatchState{}
        .last_inactive_notification_synthetic);
    static_assert(!ShouldSuppressMode1TerminalPeerDepartureNotification(
        false, true, true, false));
    static_assert(!ShouldSuppressMode1TerminalPeerDepartureNotification(
        true, false, true, false));
    static_assert(!ShouldSuppressMode1TerminalPeerDepartureNotification(
        true, true, false, false));
    static_assert(ShouldSuppressMode1TerminalPeerDepartureNotification(
        true, true, true, false));
    static_assert(ShouldSuppressMode1TerminalPeerDepartureNotification(
        true, true, false, true));
    static_assert(ShouldOpenMode1ReliableWaitDialog(false));
    static_assert(!ShouldOpenMode1ReliableWaitDialog(true));
    static_assert(!ShouldCaptureMode1RemoteInactiveDrop(
        1, false, 1, 0, false));
    static_assert(ShouldCaptureMode1RemoteInactiveDrop(
        1, true, 1, 0, false));
    static_assert(!ShouldCaptureMode1RemoteInactiveDrop(
        1, true, 0, 0, false));
    static_assert(!ShouldCaptureMode1RemoteInactiveDrop(
        1, true, 1, 0, true));

    // Generic/P2P "Quit Program" is retained until the synchronized match
    // loop ends, then closes the application instead of restoring WizardNet.
    assert(ShouldCloseApplicationAfterP2PMatch(false, true));
    assert(ShouldCloseApplicationAfterP2PMatch(true, false));
    assert(ResolveGameplayPostSessionFrontendRoute(0, false, false, false) ==
        GameplayPostSessionFrontendRoute::wizardnet);
    assert(ResolveGameplayPostSessionFrontendRoute(0, false, true, true) ==
        GameplayPostSessionFrontendRoute::none);
    return 0;
}
