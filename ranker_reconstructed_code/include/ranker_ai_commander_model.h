#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ranker {

inline constexpr std::size_t kCommanderVectorSize = 528;
inline constexpr std::size_t kCommanderMapSize = 9 * 16 * 16;
inline constexpr std::size_t kCommanderPrivilegedSize = 32;
inline constexpr std::size_t kCommanderHeadCount = 8;
inline constexpr std::size_t kCommanderLogitCount = 95;
inline constexpr std::array<std::size_t, 8> kCommanderHeadSizes{42,16,4,8,16,3,3,3};
inline constexpr std::array<std::size_t, 8> kCommanderHeadOffsets{0,42,58,62,70,86,89,92};
// Changes to feature order, action semantics, or network layout require a new schema.
inline constexpr u32 kCommanderSchemaHash = 0x1f364207U;
inline constexpr u64 kCommanderSchema = kCommanderSchemaHash;

struct CommanderInput {
    std::array<float, kCommanderVectorSize> vector{};
    std::array<float, kCommanderMapSize> map{}; // channel, y, x (NCHW)
    std::array<float, kCommanderPrivilegedSize> privileged{}; // critic only
};
using CommanderMask = std::array<u8, kCommanderLogitCount>;
using CommanderAction = std::array<u8, kCommanderHeadCount>;
struct CommanderDecision {
    CommanderAction action{};
    CommanderMask mask{}; // actual conditional masks used for this action
    std::array<float, kCommanderHeadCount> logp{};
    std::array<float, kCommanderLogitCount> logits{}; // before masking
    float value = 0;
};
using CommanderHeadMask = std::function<void(std::size_t, const CommanderAction&, CommanderMask&)>;

// Independent from both the gameplay generator and std::rand.
class CommanderPcg32 {
public:
    void seed(u64 seed_value, u32 owner, u32 weight_version);
    u32 next();
    double uniform(); // [0, 1)
private:
    u64 state_ = 0;
    u64 increment_ = 1442695040888963407ULL;
};

class CommanderModel {
public:
    // A failed load leaves the previous complete model intact.
    bool load(const std::string& path, std::string* error = nullptr);
    bool loaded() const { return !tensors_.empty(); }
    u32 version() const { return version_; }
    CommanderDecision decide(const CommanderInput& input, const CommanderMask& base_mask,
                             CommanderPcg32& rng, bool deterministic = false,
                             const CommanderHeadMask& head_mask_callback = {}) const;
private:
    std::vector<std::vector<float>> tensors_;
    u32 version_ = 0;
};

} // namespace ranker
