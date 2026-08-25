#pragma once

namespace ranker {

// Link lobby roles 0..3 belong to the original protocol. Keep AI Play as a
// reconstructed-client-only presentation value and serialize it as the
// original Computer role so startup packets and replays remain compatible.
constexpr int kAiPlayLinkLobbyRoleValue = 4;
constexpr int kOriginalComputerLinkLobbyRoleValue = 3;

constexpr bool IsAiPlayLinkLobbyRole(int role_value) {
    return role_value == kAiPlayLinkLobbyRoleValue;
}

constexpr bool IsComputerLikeLinkLobbyRole(int role_value) {
    return role_value == kOriginalComputerLinkLobbyRoleValue ||
        IsAiPlayLinkLobbyRole(role_value);
}

constexpr int SerializeAiPlayLinkLobbyRole(int role_value) {
    return IsAiPlayLinkLobbyRole(role_value) ?
        kOriginalComputerLinkLobbyRoleValue : role_value;
}

} // namespace ranker
