#include "ranker_d3d9_cubic_contract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using namespace ranker;

[[noreturn]] void fail(const char* message) {
    std::cerr << "D3D9_CUBIC_PRESENTATION_FAIL " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

bool close(float lhs, float rhs) {
    return std::fabs(lhs - rhs) < 0.001f;
}

void verify_full_frame_geometry() {
    const auto vertices = BuildD3D9CubicVertices(1600, 1200);
    require(close(vertices[1].x, -0.5f) && close(vertices[1].y, -0.5f),
        "full-frame origin lost its D3D9 half-pixel alignment");
    require(close(vertices[2].x, 1599.5f) && close(vertices[2].y, 1199.5f),
        "full-frame extent does not cover the output");
}

void verify_cursor_is_a_separate_point_sampled_quad() {
    const auto geometry = BuildD3D9CursorOverlayGeometry(1600, 1200, 10, 20);
    require(geometry.visible, "onscreen cursor geometry is hidden");
    require(geometry.source_left == 0 && geometry.source_top == 0 &&
            geometry.source_right == 32 && geometry.source_bottom == 32,
        "onscreen cursor source was clipped");
    require(close(geometry.vertices[1].x, 19.5f) &&
            close(geometry.vertices[1].y, 39.5f),
        "cursor origin was not scaled independently");
    require(close(geometry.vertices[2].x, 83.5f) &&
            close(geometry.vertices[2].y, 103.5f),
        "cursor size did not follow output resolution");
}

void verify_cursor_edge_clipping_preserves_source_offset() {
    const auto geometry = BuildD3D9CursorOverlayGeometry(800, 600, -5, -7);
    require(geometry.visible, "partially visible cursor was rejected");
    require(geometry.source_left == 5 && geometry.source_top == 7 &&
            geometry.source_right == 32 && geometry.source_bottom == 32,
        "negative cursor coordinates produced the wrong source clip");
    require(close(geometry.vertices[1].x, -0.5f) &&
            close(geometry.vertices[1].y, -0.5f),
        "clipped cursor did not begin at the output boundary");

    const auto hidden = BuildD3D9CursorOverlayGeometry(800, 600, -32, 100);
    require(!hidden.visible, "fully offscreen cursor remained visible");
}

} // namespace

int main() {
    verify_full_frame_geometry();
    verify_cursor_is_a_separate_point_sampled_quad();
    verify_cursor_edge_clipping_preserves_source_offset();
    return EXIT_SUCCESS;
}
