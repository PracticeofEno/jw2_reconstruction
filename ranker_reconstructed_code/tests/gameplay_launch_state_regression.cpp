#include "ranker_gameplay_launch.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace ranker;

    require(GameplayLaunchUsesReplay(GameplayLaunchSource::replay),
        "replay route must consume only replay state");
    require(!GameplayLaunchUsesLinkLobby(GameplayLaunchSource::replay),
        "replay route must not consume a retained Link lobby");
    require(GameplayLaunchUsesLinkLobby(GameplayLaunchSource::link_lobby),
        "Link route must consume the current Link handoff");
    require(GameplayLaunchUsesLinkLobby(GameplayLaunchSource::command_line_p2p),
        "command-line joins may receive their map through Link");
    require(!GameplayLaunchUsesCommandLineP2P(GameplayLaunchSource::wizard_session),
        "Wizard route must not inherit command-line parameters");
    require(GameplayLaunchUsesLivePeerConnection(GameplayLaunchSource::link_lobby),
        "Link sessions need the peer startup cursor transition");
    require(GameplayLaunchUsesLivePeerConnection(
            GameplayLaunchSource::command_line_p2p),
        "direct P2P joins need the peer startup cursor transition");
    require(GameplayLaunchUsesLivePeerConnection(
            GameplayLaunchSource::wizard_session),
        "Wizard sessions need the peer startup cursor transition");
    require(!GameplayLaunchUsesLivePeerConnection(GameplayLaunchSource::replay),
        "replay startup must not inherit peer cursor handling");

    std::array<u8, 0x20ff> replay{};
    replay[0x5f] = 2;
    replay[0x63 + 0x50 + 2] = 3;
    require(ResolveReplayGameplayTheme(replay.data(), replay.size()) == 3,
        "replay theme must come from its recorded local tribe");
    replay[0x63 + 0x50 + 2] = 7;
    require(ResolveReplayGameplayTheme(replay.data(), replay.size()) == 0,
        "invalid recorded tribes must use the default theme");
    require(ResolveReplayGameplayTheme(replay.data(), 0x60) == 0,
        "truncated replay metadata must not read retained lobby state");

    std::cout << "gameplay launch state regression: PASS\n";
    return EXIT_SUCCESS;
}
