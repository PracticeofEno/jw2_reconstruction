#include "ranker_frontend_layout.h"

using namespace ranker;

namespace {

constexpr FrontendLayoutRect kBounds{100, 50, 800, 600};

static_assert(FrontendLayoutOrigin(kBounds).x == 100);
static_assert(FrontendLayoutOrigin(kBounds).y == 50);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 400, 300).x == 300);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 400, 300).y == 200);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 1200, 800).x == -100);
static_assert(CenteredFrontendLayoutOrigin(kBounds, 1200, 800).y == -50);
static_assert(
    CenteredContainedFrontendLayoutOrigin(kBounds, 1200, 800).x == 100);
static_assert(
    CenteredContainedFrontendLayoutOrigin(kBounds, 1200, 800).y == 50);

} // namespace

int main() {
    return 0;
}
