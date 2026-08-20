#include "ranker_gameplay_cheats.h"
#include "ranker_gameplay_packets.h"
#include "ranker_player_slots.h"
#include "ranker_gameplay_sound.h"
#include "ranker_gameplay_session_format.h"
#include "ranker_gameplay_session_flow.h"
#include "ranker_gameplay_session_rules.h"
#include "ranker_gameplay_visibility.h"

#include <array>
#include <cassert>

int main() {
    using namespace ranker;

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
