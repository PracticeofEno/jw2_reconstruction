#include "ranker_unit_movement.h"

int main() {
    ranker::UnitMovementUnit source{};
    ranker::UnitMovementUnit linked{};
    source.saved_path_target_x = 0x1234;
    source.saved_path_target_y = -0x2345;

    ranker::ApplyMode1Subtype08AuxVector(
        source, &linked, 0x3456, -0x4567, 0x5678);

    if (source.linked_object_id != 0x3456 ||
        source.linked_unit != &linked ||
        source.next_path_x != -0x4567 ||
        source.next_path_y != 0x5678) {
        return 1;
    }
    if (source.saved_path_target_x != 0x1234 ||
        source.saved_path_target_y != -0x2345) {
        return 2;
    }
    return 0;
}
