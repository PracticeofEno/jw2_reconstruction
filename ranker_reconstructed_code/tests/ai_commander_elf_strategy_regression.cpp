#include "ranker_ai_commander.h"
#include "ranker_ai_commander_elf_strategy.inc"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ranker;
namespace {
void require(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
void rejects(const std::string& bytes) {
    bool rejected = false;
    try { ParseElfCemProfile(bytes); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid profile was silently accepted");
}
void strict_profile_contract() {
    const auto zero = ParseElfCemProfile("JWELFSTRAT1\n0 0 0 0 0 0\n");
    require(zero.values == std::array<double, 6>{}, "zero profile changed its values");
    const auto low = ParseElfCemProfile("JWELFSTRAT1\n-4 -.25 -.5 -1 -8 -.3\n");
    require(low.values == std::array<double, 6>{-4, -.25, -.5, -1, -8, -.3},
            "lower bounds or signed offsets were changed");
    const auto high = ParseElfCemProfile("JWELFSTRAT1\r\n4 .5 1 4 8 .3\r\n");
    require(high.values == std::array<double, 6>{4, .5, 1, 4, 8, .3},
            "upper bounds or Windows newlines were rejected");
    const std::vector<std::string> invalid = {
        "", "WRONG\n0 0 0 0 0 0", "JWELFSTRAT1\n0 0 0 0 0",
        "JWELFSTRAT1\n0 0 0 0 0 0 extra", "JWELFSTRAT1\n0 0 0 0 0 nan",
        "JWELFSTRAT1\n0 0 0 0 0 inf", "JWELFSTRAT1\n.5 0 0 0 0 0",
        "JWELFSTRAT1\n0 0 0 .5 0 0", "JWELFSTRAT1\n0 0 0 0 .5 0",
        "JWELFSTRAT1\n-5 0 0 0 0 0", "JWELFSTRAT1\n5 0 0 0 0 0",
        "JWELFSTRAT1\n0 -.251 0 0 0 0", "JWELFSTRAT1\n0 .501 0 0 0 0",
        "JWELFSTRAT1\n0 0 -.501 0 0 0", "JWELFSTRAT1\n0 0 1.001 0 0 0",
        "JWELFSTRAT1\n0 0 0 -2 0 0", "JWELFSTRAT1\n0 0 0 5 0 0",
        "JWELFSTRAT1\n0 0 0 0 -9 0", "JWELFSTRAT1\n0 0 0 0 9 0",
        "JWELFSTRAT1\n0 0 0 0 0 -.301", "JWELFSTRAT1\n0 0 0 0 0 .301"
    };
    for (const auto& bytes : invalid) rejects(bytes);
    rejects(std::string("JWELFSTRAT1\n0 0 0 0 0 0") + '\0');
    rejects(std::string("JWELFSTRAT1\n0 0 0 0 0 0") + char(0x80));
    rejects("JWELFSTRAT1\n0 0 0 0 0 0" + std::string(4096, ' '));
}
}
int main() {
    try { strict_profile_contract(); }
    catch (const std::exception& e) {
        std::cerr << "ai_commander_elf_strategy_regression: " << e.what() << '\n';
        return 1;
    }
    std::cout << "Elf strategy: zero/bounds/CRLF and 24 rejection cases passed\n";
}
