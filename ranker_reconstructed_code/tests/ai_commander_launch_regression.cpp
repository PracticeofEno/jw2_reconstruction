#include "ranker_p2p_lobby.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace ranker;
void require(bool result, const char* message) {
    if (!result) { std::cerr << "commander launch: " << message << '\n'; std::exit(1); }
}
bool parse(P2PNetworkLaunchParameters& options, const std::string& command) {
    return ParseP2PNetworkCommandLine(options, command.c_str());
}
}

int main() {
    using namespace ranker;
    P2PNetworkLaunchParameters options;
    require(parse(options, "-AISELF -AITEACHER -AIDETERMINISTIC -AINOSLEEP -AIAUTOSCOUT:0 -AICURRICULUM:4 -SEED:901"),
        "teacher launch rejected");
    require(options.self_play && options.self_play_commander && options.self_play_teacher &&
        options.self_play_scripted && options.self_play_deterministic && options.self_play_no_sleep &&
        !options.self_play_autoscout && options.self_play_curriculum == 4 && options.self_play_seed == 901,
        "teacher flags did not reach runtime options");
    require(parse(options, R"(-AISELF -AIWEIGHTS:"C:\Models\Policy A.bin" -AIROLLOUT:"C:\Run One\session.rlo" -AIOUT:"C:\Run One" -SEED:18)"),
        "quoted values rejected");
    require(std::string(options.self_play_weights.data()) == R"(C:\Models\Policy A.bin)" &&
        std::string(options.self_play_rollout.data()) == R"(C:\Run One\session.rlo)" &&
        std::string(options.self_play_output_dir.data()) == R"(C:\Run One)",
        "quoted path casing, spaces, or backslashes changed");
    require(options.self_play_commander && !options.self_play_teacher && !options.self_play_no_sleep &&
        !options.self_play_deterministic && options.self_play_autoscout && options.self_play_curriculum == 2,
        "new commander flags leaked across parse");
    require(parse(options, R"(-AISELF "-AIWEIGHTS:C:\Models\Policy A.bin" "-AIWEIGHTS2:C:\Models\Policy B.bin" "-AIROLLOUT:C:\Run Two\session.rlo" "-AIOUT:C:\Run Two" -AIVS -SEED:19)"),
        "whole-argument quoting from Windows subprocess rejected");
    require(options.self_play_versus &&
        std::string(options.self_play_weights2.data()) == R"(C:\Models\Policy B.bin)" &&
        std::string(options.self_play_rollout.data()) == R"(C:\Run Two\session.rlo)" &&
        std::string(options.self_play_output_dir.data()) == R"(C:\Run Two)",
        "whole-argument quoted paths parsed incorrectly");
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -AITEACHERVAR:3 -SEED:5"), "variant flag launch rejected");
    require(options.self_play_commander && !options.self_play_teacher && !options.self_play_teacher2 &&
        options.self_play_teacher_variant == 3, "-AITEACHERVAR: must not enable the teacher by prefix match");
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -AIVS -AITEACHER2 -AITEACHERVAR2:7 -SEED:5"), "policy-vs-teacher launch rejected");
    require(options.self_play_commander && !options.self_play_teacher && options.self_play_teacher2 &&
        options.self_play_versus && options.self_play_teacher_variant2 == 7, "-AITEACHER2 flags did not reach runtime options");
    P2PNetworkLaunchParameters lone_teacher2;
    require(!parse(lone_teacher2, "-AISELF -AIWEIGHTS:policy.bin -AITEACHER2 -SEED:5"), "-AITEACHER2 without -AIVS must be rejected");
    require(parse(options, "-AISELF -AIRANDOM"), "legacy random launch rejected after commander");
    require(!options.self_play_commander && !options.self_play_teacher && options.self_play_random &&
        !options.self_play_versus && options.self_play_weights[0] == '\0' &&
        options.self_play_weights2[0] == '\0' && options.self_play_rollout[0] == '\0',
        "commander weights or mode leaked into legacy launch");
    require(parse(options, "-AISELF -AICOMMANDER -AIRANDOM"), "commander random launch rejected");
    require(options.self_play_commander && options.self_play_seed != 0, "commander default seed remains zero");
    require(parse(options, "-AISELF -AIACT3:18100"), "legacy entity launch rejected");
    require(!options.self_play_commander && options.self_play_act3_port == 18100,
        "legacy entity launch unexpectedly activated commander");
    for (const char* incompatible : {"-AIIPC:18100", "-AIACT3:18100", "-AIENTITY:18100",
            "-AIREVEALBASE", "-AISHADOW", "-AISHADOW2", "-AIIMITATE", "-AIREPLAY:x.ply",
            "-AICURRICULUM:5"}) {
        P2PNetworkLaunchParameters fresh;
        require(!parse(fresh, std::string("-AISELF -AITEACHER ") + incompatible),
            "incompatible commander legacy/debug flag accepted");
    }
    for (const char* malformed : {"-AIWEIGHTS:", "-AIWEIGHTS:\"unterminated path",
            "-AIWEIGHTS2:second.bin", "-AITEACHER -AIWEIGHTS2:second.bin"}) {
        P2PNetworkLaunchParameters fresh;
        require(!parse(fresh, std::string("-AISELF ") + malformed),
            "malformed commander path/second-policy combination accepted");
    }
    P2PNetworkLaunchParameters oversized;
    require(!parse(oversized, "-AISELF -AIWEIGHTS:" + std::string(4096, 'a')),
        "oversized weights path silently truncated");
    P2PNetworkLaunchParameters normal;
    require(!parse(normal, "-AIWEIGHTS:policy.bin -AINOSLEEP") &&
        !normal.self_play_commander && !normal.self_play_no_sleep,
        "commander flags escaped the self-play-only entry");
    std::cout << "commander launch reparse/path/isolation regression passed\n";
}
