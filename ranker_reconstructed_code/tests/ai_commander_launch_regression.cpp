#include "ranker_p2p_lobby.h"
#include "ranker_ai_commander_model.h"

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
std::array<u32, 32> policy_stream(const P2PNetworkLaunchParameters& options, u32 owner, u32 version) {
    CommanderPcg32 rng;
    const auto seed = options.commander_rng_seed(version);
    rng.seed(seed.first, owner, seed.second);
    std::array<u32, 32> values{};
    for (auto& value : values) value = rng.next();
    return values;
}

void policy_seed_regression() {
    P2PNetworkLaunchParameters options;
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -SEED:901"), "default policy seed launch rejected");
    require(!options.self_play_has_policy_seed && options.self_play_seed == 901,
        "omitted policy seed changed game seed or enabled override");
    CommanderPcg32 original;
    original.seed(901, 3, 17);
    const auto default_stream = policy_stream(options, 3, 17);
    for (u32 value : default_stream) require(value == original.next(), "default policy RNG sequence changed");
    require(policy_stream(options, 3, 8) != policy_stream(options, 3, 9),
        "default stream lost the model-version salt");

    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -SEED:901 -AIPOLICYSEED:0"),
        "explicit zero policy seed rejected");
    require(options.self_play_has_policy_seed && options.self_play_policy_seed == 0 && options.self_play_seed == 901,
        "explicit zero confused with omission or replaced game seed");
    const auto zero_stream = policy_stream(options, 3, 8);
    require(zero_stream == policy_stream(options, 3, 9), "override still depends on model version");
    require(zero_stream != policy_stream(options, 4, 8), "override lost owner separation");
    CommanderPcg32 explicit_reference;
    explicit_reference.seed(0, 3, 0);
    for (u32 value : zero_stream) require(value == explicit_reference.next(), "override seed mix differs from (N,owner,0)");

    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -SEED:7 -AIPOLICYSEED:0"),
        "second explicit zero policy seed rejected");
    require(policy_stream(options, 3, 9) == zero_stream && options.self_play_seed == 7,
        "override remains tied to game seed");
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -SEED:7 -AIPOLICYSEED:1"),
        "positive policy seed rejected");
    require(policy_stream(options, 3, 9) != zero_stream, "distinct policy seeds yielded identical streams");
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -AIPOLICYSEED:18446744073709551615"),
        "maximum u64 policy seed rejected");
    require(options.self_play_policy_seed == ~u64{0}, "maximum u64 policy seed truncated");
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin \"-aipolicyseed:0042\" -SEED:17"),
        "quoted lower-case policy seed rejected");
    require(options.self_play_policy_seed == 42 && options.self_play_seed == 17,
        "quoted policy seed or game seed parsed incorrectly");
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -AIPOLICYSEED:\"43\""),
        "quoted policy seed value rejected");
    require(options.self_play_policy_seed == 43, "quoted policy seed value changed");

    for (const char* malformed : {"-AIPOLICYSEED", "-AIPOLICYSEED:", "-AIPOLICYSEED:-1",
            "-AIPOLICYSEED:+1", "-AIPOLICYSEED:1.0", "-AIPOLICYSEED:1x", "-AIPOLICYSEED:0x10",
            "-AIPOLICYSEED:18446744073709551616", "-AIPOLICYSEED:999999999999999999999999",
            "-AIPOLICYSEED:1 -AIPOLICYSEED:1", "-AIPOLICYSEED:\"7"}) {
        require(!parse(options, std::string("-AISELF -AIWEIGHTS:policy.bin ") + malformed),
            "malformed/overflow/duplicate policy seed accepted");
    }
    require(parse(options, R"(-AISELF -AIWEIGHTS:"C:\Models\path -AIPOLICYSEED:99.bin" -SEED:9)"),
        "flag-like path substring rejected");
    require(!options.self_play_has_policy_seed && options.self_play_policy_seed == 0 && options.self_play_seed == 9,
        "flag-like path substring changed policy RNG or override leaked across parses");
    require(!parse(options, "-AISELF -AIRANDOM -AIPOLICYSEED:9"),
        "policy seed accepted outside commander mode");
    require(!parse(options, "-AIPOLICYSEED:9") && !options.self_play_has_policy_seed,
        "policy seed escaped self-play-only parser");

    std::srand(777);
    const int expected_game_random = std::rand();
    std::srand(777);
    require(parse(options, "-AISELF -AIWEIGHTS:policy.bin -SEED:23 -AIPOLICYSEED:4"),
        "RNG isolation launch rejected");
    policy_stream(options, 1, 9);
    require(std::rand() == expected_game_random && options.self_play_seed == 23,
        "policy seed parsing/initialization consumed or reseeded game/slot RNG");
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
    policy_seed_regression();
    for(u32 tribe=0;tribe<4;++tribe) {
        require(parse(options,"-AISELF -AIWEIGHTS:policy.bin -AIVS -AIOWNTRIBE:"+
            std::to_string(tribe)+" -AIOWNTRIBE2:"+std::to_string((tribe+1)%4)),
            "cross-race launch rejected");
        require(options.self_play_own_tribe==tribe&&options.self_play_own_tribe2==(tribe+1)%4,
            "policy races parsed incorrectly");
    }
    require(parse(options,R"(-AISELF -AIWEIGHTS:"C:\Models\path -AIOWNTRIBE:0.bin")")&&
        options.self_play_own_tribe==2&&options.self_play_own_tribe2==2,
        "race default leaked or path substring changed race");
    require(parse(options,R"(-AISELF -AIWEIGHTS:policy.bin "-aiowntribe:0")")&&
        options.self_play_own_tribe==0,"quoted race argument rejected");
    for(const char* malformed:{"-AIOWNTRIBE", "-AIOWNTRIBE:", "-AIOWNTRIBE:-1", "-AIOWNTRIBE:4",
        "-AIOWNTRIBE:1.5", "-AIOWNTRIBE:0 -AIOWNTRIBE:0", "-AIOWNTRIBE2:4"})
        require(!parse(options,std::string("-AISELF -AIWEIGHTS:policy.bin ")+malformed),
            "invalid or duplicate policy race accepted");
    require(!parse(options,"-AISELF -AIRANDOM -AIOWNTRIBE:1"),"race flag escaped Commander");
    require(!parse(options,"-AI1V1 -AIWEIGHTS:policy.bin -AIOWNTRIBE:1"),
        "unsupported legacy owner-zero launch silently ignored the chosen race");
    P2PNetworkLaunchParameters transfer_options;
    require(parse(transfer_options, "-AISELF -AIWEIGHTS:policy.bin -AICOORDINATEDTRANSFERS:1") &&
        transfer_options.self_play_coordinated_transfers, "coordinated transfers opt-in rejected");
    require(parse(transfer_options, "-AISELF -AIWEIGHTS:policy.bin") &&
        !transfer_options.self_play_coordinated_transfers, "coordinated transfers leaked between launches");
    require(parse(transfer_options, "-AISELF -AIWEIGHTS:policy.bin -AICOORDINATEDTRANSFERS:0") &&
        !transfer_options.self_play_coordinated_transfers, "explicit baseline transfers changed");
    require(parse(transfer_options, "-AISELF -AIRANDOM -AICOORDINATEDTRANSFERS:1") &&
        !transfer_options.self_play_coordinated_transfers, "coordinated transfers escaped commander mode");
    std::cout << "commander launch reparse/path/isolation regression passed\n";
}
