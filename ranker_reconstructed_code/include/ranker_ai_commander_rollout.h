#pragma once

#include "ranker_ai_commander_model.h"

#include <fstream>

namespace ranker {

// Every file is one owner/episode pinned to one immutable policy version.
// Incomplete files have no terminal record and are rejected by the learner.
enum class CommanderRolloutStatus : u8 { decision, win, loss, truncated, invalid };
inline constexpr u32 kCommanderRolloutFormatVersion = 2;
inline constexpr u32 kCommanderRolloutRecordBytes = 3522;

class CommanderRolloutWriter {
public:
    bool open(const std::string& path, u32 owner, u32 seed, u32 policy_version);
    // A terminal at the most recent decision's frame replaces that decision:
    // its sampled action had no elapsed transition and must not enter training.
    // Ordinary decisions must increase in frame; nothing follows a terminal.
    // `label` (optional, DAgger): the rule commander's decision for the same
    // observation while the learned policy acted. Labels go to a parallel
    // "<path>.teacher.bin" file with one 20-byte record (95-bit conditional
    // mask packed in 12 bytes + 8 head actions) per RLO record; the terminal
    // record gets zeros. Once a label is written every later record must
    // carry one (terminal excepted) so the two files stay index-aligned.
    bool append(u32 frame, u8 event, bool teacher, CommanderRolloutStatus status,
        const CommanderInput& input, const CommanderDecision& decision,
        const std::array<float, 4>& potential, const CommanderDecision* label = nullptr);
    void close();
    bool is_open() const { return stream_.is_open(); }
private:
    std::ofstream stream_;
    std::ofstream decisions_;
    std::ofstream labels_;
    std::string rollout_path_, decisions_path_, labels_path_;
    std::streampos last_record_offset_{0}, last_decision_offset_{0}, last_label_offset_{0};
    u32 last_frame_ = 0;
    u32 previous_frame_ = 0, policy_version_ = 0;
    CommanderRolloutStatus last_status_ = CommanderRolloutStatus::decision;
    bool has_record_ = false;
};

// Use the exact stored precision before inference: IEEE binary16 vectors and
// privileged features, and uint8/255 maps. This prevents on-policy mismatch.
void QuantizeCommanderMap(CommanderInput& input);

} // namespace ranker
