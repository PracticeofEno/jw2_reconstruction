#include "ranker_d3d9_cubic_contract.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "D3D9_CUBIC_PRESENTATION_FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace ranker;

    require(kD3D9CubicTextureWidth == 1024 &&
            kD3D9CubicTextureHeight == 1024,
        "texture is not the original 1024x1024 surface");
    require(kD3D9CubicTextureCount == 2,
        "managed upload textures no longer rotate as a pair");
    require(IsD3D9CubicRendererSupported("auto") &&
            IsD3D9CubicRendererSupported("direct3d9"),
        "native Direct3D9 renderer choices were rejected");
    require(!IsD3D9CubicRendererSupported("direct3d9on12") &&
            !IsD3D9CubicRendererSupported("opengl"),
        "an unimplemented renderer escaped to Direct3DCreate9");
    require(MayProbeD3D9CooperativeLevel(7, 7) &&
            !MayProbeD3D9CooperativeLevel(8, 7) &&
            !MayProbeD3D9CooperativeLevel(0, 0),
        "TestCooperativeLevel is no longer owner-thread-only");
    require(sizeof(kD3D9CatmullRomPixelShader) == 1200,
        "Catmull-Rom bytecode length changed");
    require(D3D9CatmullRomShaderFnv1a64() == 0x4dd0edd1b1e752d6ull,
        "Catmull-Rom bytecode digest changed");
    require(kD3D9CatmullRomPixelShader.front() == 0xffff0200u &&
            kD3D9CatmullRomPixelShader.back() == 0x0000ffffu,
        "pixel shader model or END token changed");

    require(ShouldUseD3D9CubicPresentation(true, true, true, false,
            2, 800, 600, 16, 0xf800, 0x07e0, 0x001f),
        "x64 RGB565 FILTER_CUBIC contract was rejected");
    for (const u32 filter : {kD3D9FilterNearest, kD3D9FilterLinear,
             kD3D9FilterLanczos}) {
        require(!ShouldUseD3D9CubicPresentation(true, true, true, false,
                filter, 800, 600, 16, 0xf800, 0x07e0, 0x001f),
            "an unimplemented filter escaped to the cubic backend");
    }
    require(!ShouldUseD3D9CubicPresentation(false, true, true, false,
            2, 800, 600, 16, 0xf800, 0x07e0, 0x001f),
        "the bundled x86 wrapper path was intercepted");
    require(!ShouldUseD3D9CubicPresentation(true, false, true, false,
            2, 800, 600, 16, 0xf800, 0x07e0, 0x001f),
        "exclusive DirectDraw incorrectly selected the windowed backend");
    require(!ShouldUseD3D9CubicPresentation(true, true, false, false,
            2, 800, 600, 16, 0xf800, 0x07e0, 0x001f),
        "a non-D3D9 renderer escaped to the cubic backend");
    require(!ShouldUseD3D9CubicPresentation(true, true, true, true,
            2, 800, 600, 16, 0xf800, 0x07e0, 0x001f),
        "boxing mode escaped to the unboxed cubic backend");

    const auto vertices = BuildD3D9CubicVertices(1280, 960);
    require(vertices[0].x == -0.5f && vertices[1].y == -0.5f &&
            vertices[2].x == 1279.5f && vertices[2].y == 959.5f,
        "D3D9 half-pixel corrected output quad changed");
    require(vertices[2].u == 800.0f / 1024.0f &&
            vertices[2].v == 600.0f / 1024.0f,
        "logical RGB565 texture coordinates changed");

    std::cout <<
        "D3D9_CUBIC_PRESENTATION_PASS filter=2 textures=2x1024x1024 "
        "shader_bytes=1200 quad=half-pixel renderer=auto,direct3d9 "
        "coop_probe=owner-only boxing=off "
        "fallback_filters=0,1,3\n";
    return EXIT_SUCCESS;
}
