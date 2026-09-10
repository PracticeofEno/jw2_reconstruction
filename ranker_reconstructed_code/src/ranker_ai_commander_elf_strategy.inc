// Optional six-parameter Elf strategy profile for the rule commander.
// The NN actor and gameplay rules do not use this profile. Zero offsets retain
// the frozen scaffold expressions; the runner records whether reward learning
// has actually been applied to a particular profile.
#pragma once
#include "ranker_startup_environment.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ranker { namespace {
struct ElfCemProfile {
    // Worker offset, Red scale, frontline scale, Mana offset, entry army
    // offset, entry strength-ratio offset. Order is part of JWELFSTRAT1.
    std::array<double, 6> values{};
};

static ElfCemProfile ParseElfCemProfile(const std::string& bytes) {
    if (bytes.empty() || bytes.size() > 4096)
        throw std::runtime_error("Elf strategy profile size out of bounds");
    for (unsigned char c : bytes)
        if (c == 0 || c > 127)
            throw std::runtime_error("Elf strategy profile must be ASCII without NUL");
    std::istringstream input(bytes);
    input.imbue(std::locale::classic());
    std::string magic;
    if (!(input >> magic) || magic != "JWELFSTRAT1")
        throw std::runtime_error("wrong Elf strategy profile version");
    constexpr double lo[] = {-4, -.25, -.5, -1, -8, -.3};
    constexpr double hi[] = {4, .5, 1, 4, 8, .3};
    ElfCemProfile result;
    for (std::size_t i = 0; i < result.values.size(); ++i) {
        double& value = result.values[i];
        if (!(input >> value) || !std::isfinite(value) || value < lo[i] || value > hi[i])
            throw std::runtime_error("invalid Elf strategy profile value");
        if ((i == 0 || i == 3 || i == 4) && value != std::trunc(value))
            throw std::runtime_error("nonintegral Elf strategy profile count offset");
    }
    std::string trailing;
    if (input >> trailing) throw std::runtime_error("trailing Elf strategy profile token");
    return result;
}

static const ElfCemProfile& ElfCemLoadedProfile() {
    // Snapshot exactly once per process. A specified invalid profile must fail
    // visibly, rather than run the zero controller under another profile's ID.
    static const ElfCemProfile profile = [] {
        const char* env = std::getenv("JW2_ELF_STRATEGY_PROFILE");
        if (!env) {
            append_startup_log("[ElfStrategyProfile] absent=1 values=0,0,0,0,0,0");
            return ElfCemProfile{};
        }
        if (!*env) throw std::runtime_error("empty JW2_ELF_STRATEGY_PROFILE");
        const std::string path = std::filesystem::absolute(env).lexically_normal().string();
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open Elf strategy profile: " + path);
        const std::string bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
        const auto result = ParseElfCemProfile(bytes);
        unsigned crc = 0xffffffffu;
        for (unsigned char c : bytes) {
            crc ^= c;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u &
                    static_cast<unsigned>(-static_cast<int>(crc & 1u)));
        }
        crc ^= 0xffffffffu;
        const auto& v = result.values;
        append_startup_log(
            "[ElfStrategyProfile] crc=%08x values=%.17g,%.17g,%.17g,%.17g,%.17g,%.17g path=%s",
            crc, v[0], v[1], v[2], v[3], v[4], v[5], path.c_str());
        return result;
    }();
    return profile;
}

static const ElfCemProfile& ElfCemProfileFor(const CommanderView& v) {
    static const ElfCemProfile zero;
    return v.services.own_tribe == 1 ? ElfCemLoadedProfile() : zero;
}
static u32 ElfCemWorkerGoal(const CommanderView& v, u32 n) {
    const int d = int(ElfCemProfileFor(v).values[0]);
    return d == 0 ? n : u32(std::max(8, int(n) + d));
}
static u32 ElfCemScaleGoal(const CommanderView& v, u32 n, std::size_t field) {
    const double d = ElfCemProfileFor(v).values[field];
    return d == 0 ? n : u32(std::max(1l, std::lround(double(n) * (1 + d))));
}
static u32 ElfCemRedGoal(const CommanderView& v, u32 n) {
    return ElfCemScaleGoal(v, n, 1);
}
static u32 ElfCemFrontlineGoal(const CommanderView& v, u32 n) {
    return ElfCemScaleGoal(v, n, 2);
}
static u32 ElfCemManaGoal(const CommanderView& v, u32 n) {
    const int d = int(ElfCemProfileFor(v).values[3]);
    return d == 0 ? n : u32(std::max(0, int(n) + d));
}
static u32 ElfCemAttackArmy(const CommanderView& v, u32 n) {
    const int d = int(ElfCemProfileFor(v).values[4]);
    return d == 0 ? n : u32(std::max(8, int(n) + d));
}
static float ElfCemAttackRatio(const CommanderView& v, float n) {
    const float d = float(ElfCemProfileFor(v).values[5]);
    return d == 0 ? n : std::max(.8f, n + d);
}
} }
