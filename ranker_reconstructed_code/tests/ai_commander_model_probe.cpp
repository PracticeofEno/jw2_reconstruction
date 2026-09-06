#include "ranker_ai_commander_model.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
template<typename T> void Read(std::istream& stream, T& value) {
    if (!stream.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("short probe input");
}
template<typename T> void Write(std::ostream& stream, const T& value) {
    if (!stream.write(reinterpret_cast<const char*>(&value), sizeof(value))) throw std::runtime_error("probe write failed");
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--rng") {
            ranker::CommanderPcg32 rng;
            rng.seed(901, 3, 17);
            for (int i = 0; i < 32; ++i) std::cout << rng.next() << '\n';
            return 0;
        }
        if (argc != 2 && argc != 4) return 1;
        ranker::CommanderModel model;
        std::string error;
        if (!model.load(argv[1], &error)) { std::cerr << error << '\n'; return 2; }
        if (argc == 2) { std::cout << model.version() << '\n'; return 0; }
        std::ifstream input(argv[2], std::ios::binary);
        std::ofstream output(argv[3], std::ios::binary);
        u32 count = 0;
        Read(input, count);
        if (count > 10000) throw std::runtime_error("probe record limit exceeded");
        ranker::CommanderPcg32 rng;
        rng.seed(901, 3, model.version());
        for (u32 row = 0; row < count; ++row) {
            ranker::CommanderInput observation;
            ranker::CommanderMask mask;
            Read(input, observation.vector); Read(input, observation.map); Read(input, observation.privileged); Read(input, mask);
            const auto decision = model.decide(observation, mask, rng, true);
            Write(output, decision.action); Write(output, decision.mask); Write(output, decision.logp);
            Write(output, decision.logits); Write(output, decision.value);
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("trailing probe input");
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 3;
    }
}
