#include "ranker_types.h"

#include <cassert>
#include <limits>

int main() {
    assert(ranker::WrappedU32ToI32(0u) == 0);
    assert(ranker::WrappedU32ToI32(0x7fffffffu) ==
        std::numeric_limits<i32>::max());
    assert(ranker::WrappedU32ToI32(0x80000000u) ==
        std::numeric_limits<i32>::min());
    assert(ranker::WrappedU32ToI32(0xffffffffu) == -1);
    return 0;
}
