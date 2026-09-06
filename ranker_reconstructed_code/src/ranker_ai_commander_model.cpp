#include "ranker_ai_commander_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ranker {
namespace {
struct TensorSpec { std::string name; std::vector<u32> shape; };
std::vector<TensorSpec> TensorSpecs() {
    std::vector<TensorSpec> specs;
    const auto layer = [&specs](const std::string& name, std::vector<u32> shape) {
        const u32 out = shape.front();
        specs.push_back({name + ".weight", std::move(shape)});
        specs.push_back({name + ".bias", {out}});
    };
    layer("vector1", {256, 528});
    layer("vector2", {256, 256});
    layer("conv1", {16, 9, 3, 3});
    layer("conv2", {32, 16, 3, 3});
    layer("map_fc", {128, 2048});
    layer("trunk", {256, 384});
    for (u32 h = 0; h < 8; ++h)
        layer("heads." + std::to_string(h), {static_cast<u32>(kCommanderHeadSizes[h]), 256 + 8 * h});
    // The last head's embedding is unused and is deliberately not exported.
    for (u32 h = 0; h < 7; ++h)
        specs.push_back({"embeddings." + std::to_string(h) + ".weight", {static_cast<u32>(kCommanderHeadSizes[h]), 8}});
    layer("value1", {64, 288});
    layer("value2", {1, 64});
    return specs;
}

u32 Crc32(const u8* data, std::size_t count) {
    u32 crc = ~0U;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

class Reader {
public:
    explicit Reader(const std::vector<u8>& bytes) : bytes_(bytes) {}
    u64 integer(std::size_t n) {
        require(n);
        u64 value = 0;
        for (std::size_t i = 0; i < n; ++i) value |= static_cast<u64>(bytes_[pos_++]) << (8 * i);
        return value;
    }
    std::string string(std::size_t n) {
        require(n);
        std::string result(reinterpret_cast<const char*>(bytes_.data() + pos_), n);
        pos_ += n;
        return result;
    }
    float real() {
        const u32 bits = static_cast<u32>(integer(4));
        float value;
        std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value)) throw std::runtime_error("non-finite commander weight");
        return value;
    }
    std::size_t position() const { return pos_; }
private:
    void require(std::size_t n) const {
        if (n > bytes_.size() - pos_) throw std::runtime_error("truncated commander weights");
    }
    const std::vector<u8>& bytes_;
    std::size_t pos_ = 0;
};

void Dense(const float* in, std::size_t inputs, float* out, std::size_t outputs,
           const std::vector<float>& weight, const std::vector<float>& bias, bool relu) {
    for (std::size_t o = 0; o < outputs; ++o) {
        const float* row = weight.data() + o * inputs;
        // Independent partial sums permit vectorization without fast-math.
        float a = 0, b = 0, c = 0, d = 0;
        std::size_t i = 0;
        for (; i + 3 < inputs; i += 4) {
            a += row[i] * in[i]; b += row[i + 1] * in[i + 1];
            c += row[i + 2] * in[i + 2]; d += row[i + 3] * in[i + 3];
        }
        float value = ((a + b) + (c + d)) + bias[o];
        for (; i < inputs; ++i) value += row[i] * in[i];
        out[o] = relu ? std::max(value, 0.0f) : value;
    }
}

void Conv(const float* in, int channels, int size, float* out, int outputs, int stride,
          const std::vector<float>& weight, const std::vector<float>& bias) {
    const int target = (size + 2 - 3) / stride + 1;
    for (int oc = 0; oc < outputs; ++oc) {
        for (int oy = 0; oy < target; ++oy) for (int ox = 0; ox < target; ++ox) {
            float sum = bias[oc];
            for (int ic = 0; ic < channels; ++ic) for (int ky = 0; ky < 3; ++ky) {
                const int iy = oy * stride + ky - 1;
                if (iy < 0 || iy >= size) continue;
                for (int kx = 0; kx < 3; ++kx) {
                    const int ix = ox * stride + kx - 1;
                    if (ix >= 0 && ix < size)
                        sum += in[(ic * size + iy) * size + ix] * weight[((oc * channels + ic) * 3 + ky) * 3 + kx];
                }
            }
            out[(oc * target + oy) * target + ox] = std::max(sum, 0.0f);
        }
    }
}

template<std::size_t N> void ValidateInput(const std::array<float, N>& values) {
    for (float value : values)
        if (!std::isfinite(value)) throw std::runtime_error("non-finite commander input");
}
} // namespace

void CommanderPcg32::seed(u64 seed_value, u32 owner, u32 weight_version) {
    state_ = 0;
    increment_ = 1442695040888963407ULL;
    next();
    state_ += seed_value * 7919ULL + static_cast<u64>(owner) * 31ULL + weight_version;
    next();
}

u32 CommanderPcg32::next() {
    const u64 old = state_;
    state_ = old * 6364136223846793005ULL + increment_;
    const u32 shifted = static_cast<u32>(((old >> 18U) ^ old) >> 27U);
    const u32 rotation = static_cast<u32>(old >> 59U);
    return (shifted >> rotation) | (shifted << ((0U - rotation) & 31U));
}

double CommanderPcg32::uniform() { return static_cast<double>(next()) / 4294967296.0; }

bool CommanderModel::load(const std::string& path, std::string* error) {
    try {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("cannot open commander weights: " + path);
        const auto size = stream.tellg();
        if (size < 56 || size > 4 * 1024 * 1024) throw std::runtime_error("invalid commander weight file size");
        std::vector<u8> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("cannot read commander weights");
        Reader reader(bytes);
        if (reader.string(8) != "JW2CMD01" || reader.integer(4) != 1)
            throw std::runtime_error("unsupported commander weight format");
        const u32 version = static_cast<u32>(reader.integer(4));
        if (reader.integer(8) != kCommanderSchema || reader.integer(4) != kCommanderVectorSize ||
            reader.integer(4) != kCommanderMapSize || reader.integer(4) != kCommanderPrivilegedSize ||
            reader.integer(4) != kCommanderHeadCount || reader.integer(4) != kCommanderLogitCount)
            throw std::runtime_error("commander weight schema mismatch");
        const auto specs = TensorSpecs();
        if (reader.integer(4) != specs.size()) throw std::runtime_error("commander tensor count mismatch");
        const u32 payload_size = static_cast<u32>(reader.integer(4));
        const u32 crc = static_cast<u32>(reader.integer(4));
        if (payload_size != bytes.size() - reader.position()) throw std::runtime_error("commander payload size mismatch");
        if (Crc32(bytes.data() + reader.position(), payload_size) != crc) throw std::runtime_error("commander weight CRC mismatch");
        std::vector<std::vector<float>> tensors;
        for (const auto& spec : specs) {
            const auto name_size = reader.integer(2);
            const auto rank = reader.integer(2);
            if (name_size != spec.name.size() || rank != spec.shape.size())
                throw std::runtime_error("commander tensor metadata mismatch");
            std::size_t elements = 1;
            for (u32 dimension : spec.shape) {
                if (reader.integer(4) != dimension) throw std::runtime_error("commander tensor shape mismatch");
                elements *= dimension;
            }
            if (reader.integer(4) != elements * 4 || reader.string(static_cast<std::size_t>(name_size)) != spec.name)
                throw std::runtime_error("commander tensor name/size mismatch");
            std::vector<float> tensor(elements);
            for (float& value : tensor) value = reader.real();
            tensors.push_back(std::move(tensor));
        }
        if (reader.position() != bytes.size()) throw std::runtime_error("trailing commander weight data");
        tensors_ = std::move(tensors);
        version_ = version;
        if (error) error->clear();
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

CommanderDecision CommanderModel::decide(const CommanderInput& input, const CommanderMask& base_mask,
                                         CommanderPcg32& rng, bool deterministic,
                                         const CommanderHeadMask& head_mask_callback) const {
    if (!loaded()) throw std::runtime_error("commander model is not loaded");
    ValidateInput(input.vector); ValidateInput(input.map); ValidateInput(input.privileged);
    for (u8 bit : base_mask) if (bit > 1) throw std::runtime_error("non-binary commander mask");
    const auto& t = tensors_;
    alignas(32) std::array<float, 256> v1{}, v2{}, trunk{};
    alignas(32) std::array<float, 4096> c1{};
    alignas(32) std::array<float, 2048> c2{};
    alignas(32) std::array<float, 128> map{};
    alignas(32) std::array<float, 384> combined{};
    Dense(input.vector.data(), 528, v1.data(), 256, t[0], t[1], true);
    Dense(v1.data(), 256, v2.data(), 256, t[2], t[3], true);
    Conv(input.map.data(), 9, 16, c1.data(), 16, 1, t[4], t[5]);
    Conv(c1.data(), 16, 16, c2.data(), 32, 2, t[6], t[7]);
    Dense(c2.data(), 2048, map.data(), 128, t[8], t[9], true);
    std::copy(v2.begin(), v2.end(), combined.begin());
    std::copy(map.begin(), map.end(), combined.begin() + 256);
    Dense(combined.data(), 384, trunk.data(), 256, t[10], t[11], true);

    CommanderDecision result;
    result.mask = base_mask;
    alignas(32) std::array<float, 312> conditioned{};
    std::copy(trunk.begin(), trunk.end(), conditioned.begin());
    for (std::size_t h = 0; h < 8; ++h) {
        const auto offset = kCommanderHeadOffsets[h], count = kCommanderHeadSizes[h];
        if (head_mask_callback) {
            auto adjusted = result.mask;
            head_mask_callback(h, result.action, adjusted);
            std::copy_n(adjusted.begin() + offset, count, result.mask.begin() + offset);
        }
        if ((h == 1 && result.action[0] != 12 && result.action[0] != 14) ||
            (h >= 3 && h <= 5 && result.action[2] == 0)) {
            std::fill_n(result.mask.begin() + offset, count, 0);
            result.mask[offset] = 1;
        }
        Dense(conditioned.data(), 256 + 8 * h, result.logits.data() + offset, count, t[12 + 2 * h], t[13 + 2 * h], false);
        float maximum = -std::numeric_limits<float>::infinity();
        std::size_t selected = 0, legal = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const float logit = result.logits[offset + i];
            if (!std::isfinite(logit)) throw std::runtime_error("non-finite commander inference");
            if (result.mask[offset + i] > 1) throw std::runtime_error("non-binary conditional commander mask");
            if (result.mask[offset + i]) {
                if (logit > maximum) { maximum = logit; selected = i; }
                ++legal;
            }
        }
        if (!legal) throw std::runtime_error("empty commander head mask");
        double sum = 0;
        std::array<double, 42> probabilities{};
        for (std::size_t i = 0; i < count; ++i) if (result.mask[offset + i]) {
            probabilities[i] = std::exp(static_cast<double>(result.logits[offset + i]) - maximum);
            sum += probabilities[i];
        }
        if (!deterministic && legal > 1) {
            double sample = rng.uniform() * sum;
            for (std::size_t i = 0; i < count; ++i) if (result.mask[offset + i]) {
                selected = i;
                sample -= probabilities[i];
                if (sample < 0) break;
            }
        }
        result.action[h] = static_cast<u8>(selected);
        result.logp[h] = legal == 1 ? 0.0f : static_cast<float>(result.logits[offset + selected] - maximum - std::log(sum));
        if (h < 7) std::copy_n(t[28 + h].data() + selected * 8, 8, conditioned.begin() + 256 + 8 * h);
    }
    alignas(32) std::array<float, 288> value_input{};
    alignas(32) std::array<float, 64> value_hidden{};
    std::copy(trunk.begin(), trunk.end(), value_input.begin());
    std::copy(input.privileged.begin(), input.privileged.end(), value_input.begin() + 256);
    Dense(value_input.data(), 288, value_hidden.data(), 64, t[35], t[36], true);
    Dense(value_hidden.data(), 64, &result.value, 1, t[37], t[38], false);
    if (!std::isfinite(result.value)) throw std::runtime_error("non-finite commander value");
    return result;
}

} // namespace ranker
