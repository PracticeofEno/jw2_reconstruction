#include "ranker_ai_commander_rollout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

namespace ranker {
namespace {
void half_word(std::vector<u8>& bytes, u16 value) {
    bytes.push_back(static_cast<u8>(value));
    bytes.push_back(static_cast<u8>(value >> 8));
}
u16 half_bits(float value) {
    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const u16 sign = static_cast<u16>((bits >> 16) & 0x8000u);
    const u32 exponent = (bits >> 23) & 0xffu;
    const u32 mantissa = bits & 0x7fffffu;
    if (exponent == 255) return static_cast<u16>(sign | 0x7c00u | (mantissa ? 0x200u : 0u));
    const i32 power = static_cast<i32>(exponent) - 127;
    if (power > 15) return static_cast<u16>(sign | 0x7c00u);
    if (power < -25) return sign;
    if (power < -14) {
        const u32 shift = static_cast<u32>(-power - 1);
        const u32 significant = mantissa | 0x800000u;
        const u32 rounded = significant + ((1u << (shift - 1)) - 1) + ((significant >> shift) & 1);
        return static_cast<u16>(sign | (rounded >> shift));
    }
    // Round to nearest, ties to even, including carry into the exponent.
    const u32 rounded = mantissa + 0xfffu + ((mantissa >> 13) & 1u);
    return static_cast<u16>(sign | ((static_cast<u32>(power + 15) << 10) + (rounded >> 13)));
}
float from_half(u16 half) {
    const u32 sign = static_cast<u32>(half & 0x8000u) << 16;
    u32 exponent = (half >> 10) & 31u;
    u32 mantissa = half & 1023u;
    u32 bits = sign;
    if (exponent == 0) {
        if (mantissa != 0) {
            i32 power = -14;
            while ((mantissa & 1024u) == 0) { mantissa <<= 1; --power; }
            bits |= static_cast<u32>(power + 127) << 23;
            bits |= (mantissa & 1023u) << 13;
        }
    } else if (exponent == 31) bits |= 0x7f800000u | (mantissa << 13);
    else bits |= ((exponent + 112u) << 23) | (mantissa << 13);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void word(std::vector<u8>& bytes, u32 value) {
    for (unsigned shift = 0; shift != 32; shift += 8)
        bytes.push_back(static_cast<u8>(value >> shift));
}
void real(std::vector<u8>& bytes, float value) {
    u32 bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    word(bytes, bits);
}
u32 crc32(const std::vector<u8>& bytes) {
    u32 crc = 0xffffffffu;
    for (u8 value : bytes) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
bool write(std::ofstream& out, const std::vector<u8>& bytes) {
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return static_cast<bool>(out);
}
}

void QuantizeCommanderMap(CommanderInput& input) {
    for (float& value : input.vector) value = from_half(half_bits(value));
    for (float& value : input.privileged) value = from_half(half_bits(value));
    for (float& value : input.map)
        value = std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f) / 255.0f;
}

bool CommanderRolloutWriter::open(const std::string& path, u32 owner, u32 seed,
    u32 policy_version) {
    close();
    stream_.clear();
    decisions_.clear();
    const std::filesystem::path file(path);
    std::error_code error;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), error);
    if (error) return false;
    stream_.open(file, std::ios::binary | std::ios::trunc);
    if (!stream_) return false;
    policy_version_ = policy_version;
    rollout_path_ = path;
    decisions_path_ = path + ".decisions.jsonl";
    // Binary mode keeps byte offsets independent of Windows CRLF translation.
    decisions_.open(decisions_path_, std::ios::binary | std::ios::trunc);
    if (!decisions_) { close(); return false; }
    std::vector<u8> header{'J','W','R','L','O','0','0','1'};
    for (u32 value : {kCommanderSchemaHash, kCommanderRolloutFormatVersion, owner, seed, policy_version,
             static_cast<u32>(kCommanderVectorSize), static_cast<u32>(kCommanderMapSize),
             kCommanderRolloutRecordBytes}) word(header, value);
    if (!write(stream_, header)) { close(); return false; }
    return true;
}

bool CommanderRolloutWriter::append(u32 frame, u8 event, bool teacher,
    CommanderRolloutStatus status, const CommanderInput& input,
    const CommanderDecision& decision, const std::array<float, 4>& potential,
    const CommanderDecision* label) {
    if (!stream_.is_open() || static_cast<u8>(status) > static_cast<u8>(CommanderRolloutStatus::invalid))
        return false;
    const bool terminal = status != CommanderRolloutStatus::decision;
    if (label != nullptr && !labels_.is_open()) {
        if (has_record_) return false; // labels must start with the first record
        labels_path_ = rollout_path_ + ".teacher.bin";
        labels_.open(labels_path_, std::ios::binary | std::ios::trunc);
        if (!labels_) return false;
        std::vector<u8> header{'J','W','T','L','0','0','0','1'};
        if (!write(labels_, header)) return false;
    }
    if (labels_.is_open() && label == nullptr && !terminal) return false;
    if (has_record_ && (last_status_ != CommanderRolloutStatus::decision ||
            frame < last_frame_ || (frame == last_frame_ && !terminal)))
        return false;
    const bool replace = has_record_ && frame == last_frame_;
    const u32 predecessor = replace ? previous_frame_ : (has_record_ ? last_frame_ : 0u);
    const u32 elapsed = frame - predecessor;
    if (elapsed > 0xffffu) return false;
    std::vector<u8> bytes;
    bytes.reserve(kCommanderRolloutRecordBytes);
    word(bytes, frame);
    half_word(bytes, static_cast<u16>(elapsed));
    bytes.insert(bytes.end(), {event, static_cast<u8>(teacher), static_cast<u8>(status), 0});
    word(bytes, policy_version_);
    for (float value : input.vector) {
        const u16 half = half_bits(value);
        if ((half & 0x7c00u) == 0x7c00u) return false;
        half_word(bytes, half);
    }
    for (float value : input.map) {
        if (!std::isfinite(value)) return false;
        bytes.push_back(static_cast<u8>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f)));
    }
    std::array<u8, 12> packed_mask{};
    for (std::size_t bit = 0; bit < decision.mask.size(); ++bit) {
        if (decision.mask[bit] > 1) return false;
        packed_mask[bit / 8] |= static_cast<u8>(decision.mask[bit] << (bit % 8));
    }
    bytes.insert(bytes.end(), packed_mask.begin(), packed_mask.end());
    bytes.insert(bytes.end(), decision.action.begin(), decision.action.end());
    for (float value : decision.logp) real(bytes, value);
    real(bytes, decision.value);
    for (float value : potential) real(bytes, value);
    const float terminal_reward = status == CommanderRolloutStatus::win ?
        1.0f + 0.3f * (1.0f - static_cast<float>(frame) / 60000.0f) :
        status == CommanderRolloutStatus::loss ? -1.0f : 0.0f;
    real(bytes, terminal_reward);
    real(bytes, 0.0f); // Sixth reward part reserved by schema.
    for (float value : input.privileged) {
        const u16 half = half_bits(value);
        if ((half & 0x7c00u) == 0x7c00u) return false;
        half_word(bytes, half);
    }
    word(bytes, crc32(bytes));
    if (bytes.size() != kCommanderRolloutRecordBytes) return false;
    std::ostringstream entry;
    entry << "{\"frame\":" << frame << ",\"delta_frame\":" << elapsed
        << ",\"weight_version\":" << policy_version_ << ",\"event\":" << unsigned(event)
        << ",\"teacher\":" << (teacher ? "true" : "false") << ",\"status\":"
        << unsigned(status) << ",\"action\":[";
    for (std::size_t head = 0; head < decision.action.size(); ++head) {
        if (head) entry << ',';
        entry << unsigned(decision.action[head]);
    }
    entry << "]}\n";
    const std::string line = entry.str();
    std::vector<u8> label_bytes;
    if (labels_.is_open()) {
        std::array<u8, 12> label_mask{};
        if (label != nullptr) for (std::size_t bit = 0; bit < label->mask.size(); ++bit) {
            if (label->mask[bit] > 1) return false;
            label_mask[bit / 8] |= static_cast<u8>(label->mask[bit] << (bit % 8));
        }
        label_bytes.insert(label_bytes.end(), label_mask.begin(), label_mask.end());
        if (label != nullptr) label_bytes.insert(label_bytes.end(), label->action.begin(), label->action.end());
        else label_bytes.insert(label_bytes.end(), 8, u8(0));
    }
    const auto record_offset = replace ? last_record_offset_ : stream_.tellp();
    const auto decision_offset = replace ? last_decision_offset_ : decisions_.tellp();
    const auto label_offset = labels_.is_open() ? (replace ? last_label_offset_ : labels_.tellp()) : std::streampos(0);
    if (record_offset == std::streampos(-1) || decision_offset == std::streampos(-1) ||
        label_offset == std::streampos(-1)) return false;
    // If either output fails after writing a terminal, remove that candidate
    // from RLO1. A diagnostic write failure must not leave a trainable episode.
    const auto invalidate = [&]() {
        const std::string path = rollout_path_;
        close();
        std::error_code ignored;
        std::filesystem::resize_file(path,
            static_cast<std::uintmax_t>(static_cast<std::streamoff>(record_offset)), ignored);
        return false;
    };
    if (replace) {
        stream_.seekp(record_offset);
        decisions_.seekp(decision_offset);
        if (labels_.is_open()) labels_.seekp(label_offset);
    }
    if (!write(stream_, bytes)) return invalidate();
    decisions_.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (labels_.is_open() && !write(labels_, label_bytes)) return invalidate();
    if (terminal) {
        stream_.flush();
        decisions_.flush();
        if (labels_.is_open()) labels_.flush();
    }
    if (!stream_ || !decisions_ || (labels_.is_open() && !labels_)) return invalidate();
    if (replace) {
        // Terminal actions usually use fewer digits than the replaced sample.
        // Remove the previous JSONL suffix rather than leaving a partial line.
        std::error_code error;
        std::filesystem::resize_file(decisions_path_,
            static_cast<std::uintmax_t>(static_cast<std::streamoff>(decision_offset)) + line.size(), error);
        if (error) return invalidate();
    }
    last_record_offset_ = record_offset;
    last_decision_offset_ = decision_offset;
    last_label_offset_ = label_offset;
    previous_frame_ = predecessor;
    last_frame_ = frame;
    last_status_ = status;
    has_record_ = true;
    return true;
}

void CommanderRolloutWriter::close() {
    if (stream_.is_open()) stream_.close();
    if (decisions_.is_open()) decisions_.close();
    if (labels_.is_open()) labels_.close();
    rollout_path_.clear();
    decisions_path_.clear();
    labels_path_.clear();
    last_record_offset_ = last_decision_offset_ = last_label_offset_ = std::streampos(0);
    last_frame_ = 0;
    previous_frame_ = 0;
    policy_version_ = 0;
    last_status_ = CommanderRolloutStatus::decision;
    has_record_ = false;
}
} // namespace ranker
