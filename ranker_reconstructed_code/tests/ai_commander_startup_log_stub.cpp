// Standalone commander tests do not link the frontend startup subsystem.
// Preserve diagnostics for profile parsing/provenance assertions in probes.
#include "ranker_startup_environment.h"
#include <cstdarg>
#include <cstdio>

namespace ranker {
void append_startup_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}
}
