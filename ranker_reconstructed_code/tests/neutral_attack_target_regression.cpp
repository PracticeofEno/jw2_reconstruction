#include "ranker_gameplay_input_actions.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "NEUTRAL_ATTACK_TARGET_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_active_list_target_keeps_command_dead_frame() {
    GameplayActionUnitState neutral{};
    neutral.offset = 0x3a0;
    neutral.owner = 8;
    neutral.type = 0x20;
    neutral.active = true;
    neutral.visible = true;
    neutral.runtime_state = 4;
    neutral.command_state = 0x10000000u;

    require(GameplayActionDispatchTargetAllowed(neutral, false),
        "command-dead active neutral was removed before packet publication");
    neutral.visible = false;
    require(!GameplayActionDispatchTargetAllowed(neutral, false),
        "hidden active neutral was accepted");
    neutral.visible = true;
    neutral.active = false;
    require(!GameplayActionDispatchTargetAllowed(neutral, false),
        "lifecycle neutral was accepted by the ordinary attack path");
}

void test_lifecycle_target_contract_stays_separate() {
    GameplayActionUnitState corpse{};
    corpse.offset = 0x570;
    corpse.active = false;
    corpse.visible = true;
    corpse.runtime_state = 4;
    corpse.runtime_flags = 4;
    require(GameplayActionDispatchTargetAllowed(corpse, true),
        "eligible lifecycle target was rejected");

    corpse.runtime_flags = 0;
    require(!GameplayActionDispatchTargetAllowed(corpse, true),
        "lifecycle target without raw +0xa0 bit 4 was accepted");
    corpse.runtime_flags = 4;
    corpse.active = true;
    require(!GameplayActionDispatchTargetAllowed(corpse, true),
        "active-list target leaked into lifecycle-only dispatch");
}

} // namespace

int main() {
    test_active_list_target_keeps_command_dead_frame();
    test_lifecycle_target_contract_stays_separate();
    std::cout << "NEUTRAL_ATTACK_TARGET_PASS "
                 "active-command-dead=packetized lifecycle=separate\n";
    return EXIT_SUCCESS;
}
