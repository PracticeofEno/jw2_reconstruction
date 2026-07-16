#include "ranker_gameplay_frame_render.h"
#include "ranker_reliable_packets.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "GAMEPLAY_CHAT_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_exact_mode1_chat_payload() {
    constexpr std::array<u8, 19> kExpected{{
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0x04,
        'A', '>', ' ', 0x00,
        0xff, 0xff, 0xff, 0x03,
        'H', 'i', 0x00,
    }};

    const std::vector<u8> packet = BuildMode1ChatPayload("A", "Hi");
    require(packet.size() == kExpected.size(),
        "sender='A' text='Hi' did not produce the original 19-byte payload");
    require(std::equal(packet.begin(), packet.end(), kExpected.begin()),
        "Mode1 chat payload bytes differ from FUN_004e7090");

    Mode1ChatPayload parsed{};
    require(ParseMode1ChatPayload(packet.data(),
                static_cast<u32>(packet.size()), parsed),
        "exact Mode1 chat payload did not parse");
    require(parsed.primary_text == "A> " && parsed.secondary_text == "Hi",
        "parsed sender or message text lost the original segment boundary");
    require(parsed.primary_color_ref == 0x00ffffffu &&
            parsed.secondary_color_ref == 0x00ffffffu,
        "parsed chat segments did not preserve the original white color refs");
}

void test_mode1_chat_parser_rejects_invalid_lengths() {
    const std::vector<u8> packet = BuildMode1ChatPayload("A", "Hi");
    Mode1ChatPayload parsed{};

    require(!ParseMode1ChatPayload(nullptr, 0, parsed),
        "null chat packet was accepted");
    require(!ParseMode1ChatPayload(packet.data(),
                static_cast<u32>(packet.size() - 1), parsed),
        "truncated secondary chat segment was accepted");

    std::vector<u8> invalid_primary = packet;
    invalid_primary[7] = 0xff;
    require(!ParseMode1ChatPayload(invalid_primary.data(),
                static_cast<u32>(invalid_primary.size()), parsed),
        "out-of-range primary segment length was accepted");

    std::vector<u8> invalid_secondary = packet;
    invalid_secondary[15] = 0xff;
    require(!ParseMode1ChatPayload(invalid_secondary.data(),
                static_cast<u32>(invalid_secondary.size()), parsed),
        "out-of-range secondary segment length was accepted");
}

void test_slash_command_wrapper_matches_server_tcp_packet() {
    constexpr std::array<u8, 28> kExpectedBeforeChecksum{{
        0x00, 0x00, 0x00, 0x00,
        0x2a, 0x00, 0x00, 0x00,
        0x1c, 0x00, 0x00, 0x00,
        0xcc,
        0xff, 0xff, 0xff, 0x04,
        'A', '>', ' ', 0x00,
        0xff, 0xff, 0xff, 0x03,
        '/', 'x', 0x00,
    }};
    const std::vector<u8> chat = BuildMode1ChatPayload("A", "/x");
    std::vector<u8> wrapped = BuildMode1WrappedCommandPacket(
        chat.data(), static_cast<u32>(chat.size()));
    require(wrapped.size() == kExpectedBeforeChecksum.size() &&
            std::equal(wrapped.begin(), wrapped.end(),
                kExpectedBeforeChecksum.begin()),
        "slash command pre-send wrapper differs from FUN_004290b0");

    u8 checksum = 0;
    for (std::size_t index = 13; index < wrapped.size(); ++index) {
        checksum = static_cast<u8>(checksum + wrapped[index] *
            static_cast<u8>((index % 9u) + 1u));
    }
    wrapped[12] = checksum;
    require(checksum == 0x8bu,
        "slash command transport checksum differs from FUN_004531b0");
}

void test_chat_channel_recipient_masks_and_defaults() {
    constexpr u32 kCustomMask = 0x000000a5u;
    constexpr u32 kRelationMask = 0x00000012u;

    require(ResolveMode1ChatRecipientMask(
                0, kCustomMask, kRelationMask) == kCustomMask,
        "channel zero did not use the explicit observer/custom target mask");
    require(ResolveMode1ChatRecipientMask(
                1, kCustomMask, kRelationMask) == kRelationMask,
        "channel one did not use the owner-relation mask");
    require(ResolveMode1ChatRecipientMask(
                2, kCustomMask, kRelationMask) == 0xedu,
        "channel two did not invert the relation mask over player slots 0..7");
    require(ResolveMode1ChatRecipientMask(
                3, kCustomMask, kRelationMask) == 0u,
        "channel three incorrectly produced a targeted-send mask");

    require(ResolveMode1DefaultChatChannel(false) == 3u,
        "normal P2P player did not default to global channel three");
    require(ResolveMode1DefaultChatChannel(true) == 0u,
        "observer did not default to custom channel zero");
}

void test_timed_chat_queue_keeps_the_newest_five_lines() {
    GameplayHudTextState state{};
    constexpr u32 kInitialTick = 100;
    constexpr u32 kTickStep = 10;

    for (u32 ordinal = 0; ordinal < 6; ++ordinal) {
        state.current_tick_ms = kInitialTick + ordinal * kTickStep;
        QueueGameplayTimedChatNotification(state,
            "P" + std::to_string(ordinal),
            "S" + std::to_string(ordinal),
            static_cast<u8>(0x20u + ordinal),
            static_cast<u8>(0x30u + ordinal));
    }

    for (std::size_t slot = 0; slot < state.timed_notifications.size(); ++slot) {
        const u32 ordinal = static_cast<u32>(slot) + 1;
        const GameplayTimedHudNotification& notification =
            state.timed_notifications[slot];
        require(notification.active,
            "six chat messages did not leave all five HUD slots active");
        require(notification.primary_text == "P" + std::to_string(ordinal) &&
                notification.secondary_text == "S" + std::to_string(ordinal),
            "HUD chat queue did not drop only the oldest line");
        require(notification.primary_color ==
                    static_cast<u8>(0x20u + ordinal) &&
                notification.secondary_color ==
                    static_cast<u8>(0x30u + ordinal),
            "HUD chat queue lost per-segment color indices while shifting");
        require(notification.expires_tick_ms ==
                    kInitialTick + ordinal * kTickStep + 8000u,
            "HUD chat line expiry was not current_tick_ms + 8000");
    }
}

} // namespace

int main() {
    test_exact_mode1_chat_payload();
    test_mode1_chat_parser_rejects_invalid_lengths();
    test_slash_command_wrapper_matches_server_tcp_packet();
    test_chat_channel_recipient_masks_and_defaults();
    test_timed_chat_queue_keeps_the_newest_five_lines();
    std::cout << "GAMEPLAY_CHAT_PASS payload_bytes=19 channels=4 "
                 "hud_lines=5 lifetime_ms=8000\n";
    return 0;
}
