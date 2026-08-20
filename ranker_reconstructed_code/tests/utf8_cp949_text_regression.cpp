#include "ranker_system_ui.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "UTF8_CP949_TEXT_FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    require(ranker::Utf8ToCp949(nullptr).empty(),
        "null input did not produce an empty string");
    require(ranker::Utf8ToCp949("").empty(),
        "empty input did not produce an empty string");
    require(ranker::Utf8ToCp949("ASCII text") == "ASCII text",
        "ASCII text changed during conversion");

    const std::string converted = ranker::Utf8ToCp949(u8"===== 즐겜 ^오^ =====");
    const std::string expected(
        "===== \xc1\xf1\xb0\xd7 ^\xbf\xc0^ =====", 21);
    require(converted == expected,
        "Korean welcome text did not convert to the expected CP949 bytes");

    std::cout << "UTF8_CP949_TEXT_PASS\n";
    return EXIT_SUCCESS;
}
