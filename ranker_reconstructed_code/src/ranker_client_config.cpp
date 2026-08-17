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

std::string executable_sibling_path(const char* file_name) {
    std::array<char, MAX_PATH> executable{};
    const DWORD length = GetModuleFileNameA(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return std::string(file_name);
    }
    std::string result(executable.data(), length);
    const std::size_t separator = result.find_last_of("\\/");
    result.resize(separator == std::string::npos ? 0 : separator + 1);
    result += file_name;
    return result;
}

const std::string& legacy_wizardnet_server_config_path() {
    static const std::string path =
        executable_sibling_path("wizardnet_server.ini");
    return path;
}

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
    static const std::string path = executable_sibling_path("ranker_client.ini");
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

RankerClientWizardNetConfig LoadRankerClientWizardNetConfig() {
    RankerClientWizardNetConfig config;
    const auto read_value = [](const char* key, char* output,
                                DWORD output_bytes) {
        if (GetPrivateProfileStringA(kWizardNetSection, key, "", output,
                output_bytes, RankerClientConfigPath().c_str()) != 0) {
            return true;
        }
        // Older deployments kept only the relay endpoint in a second file.
        // Read it as a migration fallback, but all new writes and packaged
        // configuration use ranker_client.ini.
        return GetPrivateProfileStringA(kWizardNetSection, key, "", output,
            output_bytes, legacy_wizardnet_server_config_path().c_str()) != 0;
    };

    std::array<char, 256> address{};
    if (read_value("Address", address.data(),
            static_cast<DWORD>(address.size())) && address[0] != '\0') {
        config.address = address.data();
    }

    std::array<char, 32> port_text{};
    if (read_value("Port", port_text.data(),
            static_cast<DWORD>(port_text.size()))) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(port_text.data(), &end, 10);
        while (end != nullptr && *end != '\0' &&
            std::isspace(static_cast<unsigned char>(*end))) {
            ++end;
        }
        if (end != port_text.data() && end != nullptr && *end == '\0' &&
            parsed <= std::numeric_limits<unsigned int>::max()) {
            config.port = NormalizeRankerClientWizardNetPort(
                static_cast<unsigned int>(parsed));
        }
    }
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
