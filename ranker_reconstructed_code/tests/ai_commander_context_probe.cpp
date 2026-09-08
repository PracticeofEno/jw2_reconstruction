#include "ranker_ai_commander.h"
#include "ranker_ai_commander_rollout.h"

#include <algorithm>
#include <array>
#include <fstream>

// Input: count, then (frame, variant, four transfer frames), all uint32.
// Output: the 14 native context floats before and after rollout quantization.
// This probes the actual view builder and deployment precision used by policy
// inference; it does not invoke the teacher or run simulation/gameplay.
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    std::ifstream input(argv[1], std::ios::binary);
    std::ofstream output(argv[2], std::ios::binary);
    u32 count = 0;
    if (!input.read(reinterpret_cast<char*>(&count), sizeof(count)) || count > 1000) return 2;
    static_assert(ranker::kCommanderVectorSize == 606);
    for (u32 index = 0; index < count; ++index) {
        std::array<u32, 6> sample{};
        if (!input.read(reinterpret_cast<char*>(sample.data()), sizeof(sample))) return 3;
        ranker::AiObservation observation;
        observation.simulation_frame = sample[0];
        observation.local_owner = 0;
        observation.local_faction = 2;
        observation.local_relation_mask = 1;
        observation.map_width_tiles = observation.map_height_tiles = 32;
        observation.tiles.resize(1024);
        observation.start_x = observation.start_y = 320;
        observation.population_limit = 180;
        for (auto& tile : observation.tiles) {
            tile.passable = tile.buildable = tile.explored = tile.visible = true;
        }
        ranker::CommanderServices services;
        services.autoscout = false;
        services.teacher_variant = sample[1];
        ranker::CommanderState state;
        std::copy(sample.begin() + 2, sample.end(), state.last_transfer_frame.begin());
        auto view = ranker::BuildCommanderView(state, observation, services);
        output.write(reinterpret_cast<const char*>(view.input.vector.data() + 528), 14 * sizeof(float));
        ranker::QuantizeCommanderMap(view.input);
        output.write(reinterpret_cast<const char*>(view.input.vector.data() + 528), 14 * sizeof(float));
    }
    return output && input.peek() == std::char_traits<char>::eof() ? 0 : 4;
}
