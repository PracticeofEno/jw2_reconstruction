#include "ranker_gameplay_session_runtime.h"

#include <cstdlib>
#include <iostream>

using namespace ranker;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    require(!ShouldRejectRecoveredScenarioDuplicate(true, true),
        "serialized active-chain neighbors were treated as duplicates");
    require(ShouldRejectRecoveredScenarioDuplicate(false, true),
        "headerless recovery did not suppress an inferred duplicate");
    require(!ShouldRejectRecoveredScenarioDuplicate(false, false),
        "headerless recovery rejected a unique scenario object");

    std::cout << "SCENARIO_SERIALIZED_DUPLICATE_PASS roots=authoritative"
                 " recovery=deduplicated\n";
    return 0;
}
