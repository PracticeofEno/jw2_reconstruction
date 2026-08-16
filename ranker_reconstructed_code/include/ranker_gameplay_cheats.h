#pragma once

#include "ranker_types.h"

#include <array>
#include <string_view>

namespace ranker {

constexpr u32 kInvalidGameplayCheatCommand = 0xffffffffu;

struct GameplayCheatMatch {
    u32 command = kInvalidGameplayCheatCommand;
    bool recognized = false;
    bool uses_selected_unit = false;
};

struct GameplayCheatSignature {
    u32 byte_count = 0;
    u32 checksum = 0;
};

// FUN_004e7b68 keeps only a byte count and this weighted byte checksum for
// each local cheat phrase. The Korean phrases arrive as their CP949 bytes.
constexpr u32 GameplayCheatTextChecksum(std::string_view text) {
    u32 checksum = 0;
    u32 remaining = static_cast<u32>(text.size());
    for (const char ch : text) {
        checksum += static_cast<u8>(ch) ^ remaining;
        --remaining;
    }
    return checksum;
}

constexpr std::array<GameplayCheatSignature, 34> kGameplayCheatSignatures{{
    {16u, 0x0b59u}, {24u, 0x1006u}, {11u, 0x06a5u}, {11u, 0x06a8u},
    {11u, 0x06a7u}, {11u, 0x06aau}, {11u, 0x06a9u}, {11u, 0x06acu},
    {11u, 0x06abu}, {11u, 0x06aeu}, {0xffffu, 0xffffu},
    {0xffffu, 0xffffu}, {0xffffu, 0xffffu}, {0xffffu, 0xffffu},
    {16u, 0x0b0au}, {9u, 0x05e6u}, {10u, 0x06d9u}, {19u, 0x0cdbu},
    {10u, 0x068au}, {0xffffu, 0xffffu}, {0xffffu, 0xffffu},
    {0xffffu, 0xffffu}, {0xffffu, 0xffffu}, {13u, 0x0539u},
    {26u, 0x09aau}, {14u, 0x04e5u}, {14u, 0x04e8u}, {14u, 0x04e7u},
    {14u, 0x04eau}, {14u, 0x04e9u}, {14u, 0x04ecu}, {14u, 0x04ebu},
    {14u, 0x04eeu}, {0xffffu, 0xffffu},
}};

constexpr GameplayCheatMatch ResolveLocalGameplayCheatSignature(
    u32 byte_count, u32 checksum, bool transition_commands_restricted,
    bool selected_unit_owned_by_local_player) {
    for (u32 index = 0; index < kGameplayCheatSignatures.size(); ++index) {
        const GameplayCheatSignature signature = kGameplayCheatSignatures[index];
        if (signature.byte_count != byte_count || signature.checksum != checksum) {
            continue;
        }

        u32 command = index;
        if (index == 23u) {
            command = 0u;
        }
        else if (index == 24u) {
            command = 1u;
        }
        else if (25u <= index && index <= 32u) {
            command = index - 23u;
        }

        if (2u <= command && command <= 14u &&
            transition_commands_restricted) {
            return {};
        }
        if (command == 16u && !selected_unit_owned_by_local_player) {
            return {};
        }
        return {command, true, command == 16u};
    }
    return {};
}

constexpr GameplayCheatMatch ResolveLocalGameplayCheatText(
    std::string_view text, bool transition_commands_restricted,
    bool selected_unit_owned_by_local_player) {
    if (text.empty()) {
        return {};
    }
    return ResolveLocalGameplayCheatSignature(
        static_cast<u32>(text.size()), GameplayCheatTextChecksum(text),
        transition_commands_restricted, selected_unit_owned_by_local_player);
}

} // namespace ranker
