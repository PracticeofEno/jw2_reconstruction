#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <string>

namespace ranker {

inline const std::string& RankerDdrawIniPath() {
    static const std::string path = [] {
        std::array<char, MAX_PATH> executable{};
        const DWORD length = GetModuleFileNameA(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) {
            return std::string("ddraw.ini");
        }
        std::string result(executable.data(), length);
        const std::size_t separator = result.find_last_of("\\/");
        result.resize(separator == std::string::npos ? 0 : separator + 1);
        result += "ddraw.ini";
        return result;
    }();
    return path;
}

} // namespace ranker
#endif
