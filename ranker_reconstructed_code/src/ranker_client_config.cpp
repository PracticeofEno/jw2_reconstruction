#include "ranker_client_config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

namespace ranker {
namespace {

constexpr const char* kDisplaySection = "Display";
constexpr const char* kWizardNetSection = "WizardNet";
constexpr const char* kLastWizardAccountKey = "LastAccount";
constexpr std::size_t kMaximumWizardAccountBytes = 0x20;

bool read_integer(const char* key, int& value) {
    std::array<char, 32> text{};
    if (GetPrivateProfileStringA(kDisplaySection, key, "", text.data(),
            static_cast<DWORD>(text.size()), RankerClientConfigPath().c_str()) == 0) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text.data(), &end, 10);
    while (end != nullptr && *end != '\0' &&
        std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (end == text.data() || end == nullptr || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool read_boolean(const char* key, bool fallback) {
    std::array<char, 32> text{};
    GetPrivateProfileStringA(kDisplaySection, key,
        fallback ? "true" : "false", text.data(),
        static_cast<DWORD>(text.size()), RankerClientConfigPath().c_str());
    std::string normalized(text.data());
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
        [](unsigned char character) {
            return std::isspace(character) != 0;
        }), normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (normalized == "true" || normalized == "yes" ||
        normalized == "on" || normalized == "1") {
        return true;
    }
    if (normalized == "false" || normalized == "no" ||
        normalized == "off" || normalized == "0") {
        return false;
    }
    return fallback;
}

} // namespace

const std::string& RankerClientConfigPath() {
    static const std::string path = [] {
        std::array<char, MAX_PATH> executable{};
        const DWORD length = GetModuleFileNameA(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) {
            return std::string("ranker_client.ini");
        }
        std::string result(executable.data(), length);
        const std::size_t separator = result.find_last_of("\\/");
        result.resize(separator == std::string::npos ? 0 : separator + 1);
        result += "ranker_client.ini";
        return result;
    }();
    return path;
}

RankerClientDisplayConfig LoadRankerClientDisplayConfig() {
    RankerClientDisplayConfig config;

    int width = 0;
    int height = 0;
    if (read_integer("Width", width) && read_integer("Height", height) &&
        IsSupportedPresentationClientSize(width, height)) {
        config.width = width;
        config.height = height;
    }

    config.resizable = read_boolean("Resizable", config.resizable);
    config.border = read_boolean("Border", config.border);
    config.center = read_boolean("Center", config.center);
    config.position_set = !config.center &&
        read_integer("PosX", config.x) && read_integer("PosY", config.y);
    return config;
}

std::string LoadRankerClientLastWizardAccount() {
    std::array<char, kMaximumWizardAccountBytes> account{};
    GetPrivateProfileStringA(kWizardNetSection, kLastWizardAccountKey, "",
        account.data(), static_cast<DWORD>(account.size()),
        RankerClientConfigPath().c_str());
    return std::string(account.data());
}

bool SaveRankerClientLastWizardAccount(const char* account) {
    if (account == nullptr || account[0] == '\0') {
        return false;
    }
    return WritePrivateProfileStringA(kWizardNetSection,
        kLastWizardAccountKey, account,
        RankerClientConfigPath().c_str()) != FALSE;
}

} // namespace ranker
#endif
