#pragma once

#include "ranker_types.h"

#include <array>
#include <string_view>

namespace ranker {

constexpr u32 kInvalidGameplayCheatCommand = 0xffffffffu;
constexpr u32 kGameplayCheatPrimaryResourceBonus = 10000u;

struct GameplayCheatMatch {
    u32 command = kInvalidGameplayCheatCommand;
    bool recognized = false;
    bool uses_selected_unit = false;
};

struct GameplayCheatSignature {
    u32 byte_count = 0;
    u32 checksum = 0;
};

struct GameplayCheatTransitionRequest {
    i32 transition_index = 0;
    bool requested = false;
    bool write_transition_index = false;
    bool local_scene_change = false;
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
        // Nine table entries are literal sentinels, not hidden 65535-byte
        // cheat phrases.  The retail chat edit cannot produce them, but
        // skipping them keeps the typed resolver faithful when called directly
        // by tests or tools.
        if (signature.byte_count == 0xffffu && signature.checksum == 0xffffu) {
            continue;
        }
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

// FUN_004dd13d..FUN_004dd2e4 consumes the nested subtype-0x0d command index.
// Command zero completes the current scene, while commands 2..14 write the
// raw scenario ordinal that the outer gameplay loop increments after showing
// the result.  Commands 10..13 have handlers in ranker.exe even though their
// signature-table entries are sentinels.
constexpr GameplayCheatTransitionRequest ResolveGameplayCheatTransitionRequest(
    u32 command) {
    if (command == 0u) {
        return {-2, true, false, true};
    }
    if (command == 2u) {
        return {-1, true, true, false};
    }
    if (3u <= command && command <= 12u) {
        return {static_cast<i32>(command - 3u), true, true, false};
    }
    if (command == 13u) {
        return {10, true, true, false};
    }
    if (command == 14u) {
        return {7, true, true, false};
    }
    return {};
}

constexpr u32 ApplyGameplayCheatPrimaryResourceBonus(u32 current) {
    return current + kGameplayCheatPrimaryResourceBonus;
}

// Nested cheat command 1 first XORs the original DAT_007334c0 visibility
// gate, then passes that new value to FUN_004d6394.  Zero disables fog and
// promotes every tile; one restores the ordinary current-visibility gate.
constexpr bool GameplayFogRevealDisabledForCheatToggle(
    bool require_current_visible) {
    return !require_current_visible;
}

constexpr u32 GameplayVisibilityGateForFogCheatToggle(
    bool require_current_visible) {
    return require_current_visible ? 1u : 0u;
}

} // namespace ranker
