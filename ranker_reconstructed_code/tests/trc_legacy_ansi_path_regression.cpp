#include "ranker_trc.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "TRC_LEGACY_ANSI_PATH_FAIL " << message << '\n';
        std::exit(1);
    }
}

std::string to_ansi(const std::wstring& value) {
    BOOL used_default = FALSE;
    const int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
        value.c_str(), -1, nullptr, 0, nullptr, &used_default);
    require(required > 0 && !used_default,
        "test filename is not representable in the active ANSI code page");
    std::string result(static_cast<std::size_t>(required), '\0');
    used_default = FALSE;
    require(WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                value.c_str(), -1, result.data(), required, nullptr,
                &used_default) > 0 && !used_default,
        "ANSI filename conversion failed");
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const wchar_t non_ascii = GetACP() == 949 ? L'\u2161' : L'\u00e9';
    const fs::path path = fs::current_path() /
        (std::wstring(L"trc_legacy_ansi_") + non_ascii + L".trc");
    std::error_code ec;
    fs::remove(path, ec);

    std::array<unsigned char, 0x20> header{};
    header[0] = 'T';
    header[1] = 'R';
    header[2] = 'C';
    header[3] = 0x1a;
    header[0x0c] = 0x20; // zero-slot directory ends at the header boundary
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(output), "could not create wide test archive");
        output.write(reinterpret_cast<const char*>(header.data()),
            static_cast<std::streamsize>(header.size()));
        require(static_cast<bool>(output), "could not write test archive");
    }

    const std::string ansi_path = to_ansi(path.native());
    u32 active_records = 99;
    u32 directory_slots = 99;
    bool loaded = false;
    try {
        loaded = ranker::QueryTrcArchiveRecordCount(
            ansi_path.c_str(), &active_records, &directory_slots);
    }
    catch (const std::exception& error) {
        std::cerr << "TRC_LEGACY_ANSI_PATH_FAIL exception="
                  << error.what() << '\n';
        fs::remove(path, ec);
        return 1;
    }
    fs::remove(path, ec);

    require(loaded, "ANSI path did not resolve the wide archive filename");
    require(active_records == 0 && directory_slots == 0,
        "synthetic archive header was not parsed");
    std::cout << "TRC_LEGACY_ANSI_PATH_PASS acp=" << GetACP() << '\n';
    return 0;
}
