#pragma once

#include "ranker_types.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace ranker {

constexpr u32 kD3D9FilterNearest = 0;
constexpr u32 kD3D9FilterLinear = 1;
constexpr u32 kD3D9FilterCubic = 2;
constexpr u32 kD3D9FilterLanczos = 3;

constexpr u32 kD3D9CubicTextureWidth = 1024;
constexpr u32 kD3D9CubicTextureHeight = 1024;
constexpr u32 kD3D9CubicTextureCount = 2;
constexpr u32 kD3D9CubicLogicalWidth = 800;
constexpr u32 kD3D9CubicLogicalHeight = 600;
constexpr u32 kD3D9CursorTextureSize = 32;

constexpr bool IsD3D9CubicRendererSupported(std::string_view renderer) {
    // direct3d9on12 needs the explicit D3D9On12 creation path. This presenter
    // uses Direct3DCreate9, so only cnc-ddraw's auto/native D3D9 choices apply.
    return renderer == "auto" || renderer == "direct3d9";
}

constexpr bool MayProbeD3D9CooperativeLevel(
    u32 current_thread_id, u32 owner_thread_id) {
    // TestCooperativeLevel, CreateDevice and Reset belong to the device-owner
    // thread. Gameplay workers detect loss through upload/draw/present results.
    return owner_thread_id != 0 && current_thread_id == owner_thread_id;
}

struct D3D9CubicVertex {
    float x;
    float y;
    float z;
    float rhw;
    float u;
    float v;
};

constexpr std::array<D3D9CubicVertex, 4> BuildD3D9CubicVertices(
    u32 output_width, u32 output_height) {
    const float right = static_cast<float>(output_width) - 0.5f;
    const float bottom = static_cast<float>(output_height) - 0.5f;
    const float source_u = static_cast<float>(kD3D9CubicLogicalWidth) /
        static_cast<float>(kD3D9CubicTextureWidth);
    const float source_v = static_cast<float>(kD3D9CubicLogicalHeight) /
        static_cast<float>(kD3D9CubicTextureHeight);
    return {{
        {-0.5f, bottom, 0.0f, 1.0f, 0.0f, source_v},
        {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
        {right, bottom, 0.0f, 1.0f, source_u, source_v},
        {right, -0.5f, 0.0f, 1.0f, source_u, 0.0f},
    }};
}

struct D3D9CursorOverlayGeometry {
    std::array<D3D9CubicVertex, 4> vertices{};
    i32 source_left = 0;
    i32 source_top = 0;
    i32 source_right = 0;
    i32 source_bottom = 0;
    bool visible = false;
};

constexpr D3D9CursorOverlayGeometry BuildD3D9CursorOverlayGeometry(
    u32 output_width, u32 output_height, i32 cursor_x, i32 cursor_y) {
    D3D9CursorOverlayGeometry geometry{};
    geometry.source_left = cursor_x < 0 ? -cursor_x : 0;
    geometry.source_top = cursor_y < 0 ? -cursor_y : 0;
    geometry.source_right = cursor_x + static_cast<i32>(kD3D9CursorTextureSize) >
            static_cast<i32>(kD3D9CubicLogicalWidth)
        ? static_cast<i32>(kD3D9CubicLogicalWidth) - cursor_x
        : static_cast<i32>(kD3D9CursorTextureSize);
    geometry.source_bottom = cursor_y + static_cast<i32>(kD3D9CursorTextureSize) >
            static_cast<i32>(kD3D9CubicLogicalHeight)
        ? static_cast<i32>(kD3D9CubicLogicalHeight) - cursor_y
        : static_cast<i32>(kD3D9CursorTextureSize);
    if (output_width == 0 || output_height == 0 ||
        geometry.source_left >= geometry.source_right ||
        geometry.source_top >= geometry.source_bottom) {
        return geometry;
    }

    const float output_scale_x = static_cast<float>(output_width) /
        static_cast<float>(kD3D9CubicLogicalWidth);
    const float output_scale_y = static_cast<float>(output_height) /
        static_cast<float>(kD3D9CubicLogicalHeight);
    const float left = static_cast<float>(cursor_x + geometry.source_left) *
            output_scale_x - 0.5f;
    const float top = static_cast<float>(cursor_y + geometry.source_top) *
            output_scale_y - 0.5f;
    const float right = static_cast<float>(cursor_x + geometry.source_right) *
            output_scale_x - 0.5f;
    const float bottom = static_cast<float>(cursor_y + geometry.source_bottom) *
            output_scale_y - 0.5f;
    const float u0 = static_cast<float>(geometry.source_left) /
        static_cast<float>(kD3D9CursorTextureSize);
    const float v0 = static_cast<float>(geometry.source_top) /
        static_cast<float>(kD3D9CursorTextureSize);
    const float u1 = static_cast<float>(geometry.source_right) /
        static_cast<float>(kD3D9CursorTextureSize);
    const float v1 = static_cast<float>(geometry.source_bottom) /
        static_cast<float>(kD3D9CursorTextureSize);
    geometry.vertices = {{
        {left, bottom, 0.0f, 1.0f, u0, v1},
        {left, top, 0.0f, 1.0f, u0, v0},
        {right, bottom, 0.0f, 1.0f, u1, v1},
        {right, top, 0.0f, 1.0f, u1, v0},
    }};
    geometry.visible = true;
    return geometry;
}

constexpr bool ShouldUseD3D9CubicPresentation(bool is_64_bit, bool windowed,
    bool renderer_supported, bool boxing, u32 filter, u32 logical_width,
    u32 logical_height, u32 color_depth, u32 red_mask, u32 green_mask,
    u32 blue_mask) {
    return is_64_bit && windowed && renderer_supported && !boxing &&
        filter == kD3D9FilterCubic &&
        logical_width == kD3D9CubicLogicalWidth &&
        logical_height == kD3D9CubicLogicalHeight && color_depth == 16 &&
        red_mask == 0xf800u && green_mask == 0x07e0u && blue_mask == 0x001fu;
}

// Exact ps_2_0 bytecode used by cnc-ddraw 7.1.0 FILTER_CUBIC at commit
// 541b5de218ec3fbd6ea91e606ebfadc07c1786b0. SHA-256 of the 1200-byte
// little-endian stream: BFBC83C7A55FD3196E873ACF8F7C6475763883411E7FF178213D22648BF7F8E3.
//
// The embedded HLSL is MJP's MIT-licensed 5-fetch Catmull-Rom adaptation:
// https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
// Upstream bytecode and attribution:
// https://github.com/FunkyFr3sh/cnc-ddraw/blob/541b5de218ec3fbd6ea91e606ebfadc07c1786b0/inc/d3d9shader.h#L272-L338
// License notice: third_party/cnc-ddraw-LICENSE.txt
alignas(4) inline constexpr std::array<u32, 300> kD3D9CatmullRomPixelShader = {{
    0xffff0200u, 0x002cfffeu, 0x42415443u, 0x0000001cu, 0x00000083u, 0xffff0200u,
    0x00000002u, 0x0000001cu, 0x00000100u, 0x0000007cu, 0x00000044u, 0x00000003u,
    0x00000001u, 0x00000050u, 0x00000000u, 0x00000060u, 0x00000002u, 0x00020001u,
    0x0000006cu, 0x00000000u, 0x66727553u, 0x54656361u, 0xab007865u, 0x000c0004u,
    0x00010001u, 0x00000001u, 0x00000000u, 0x74786554u, 0x53657275u, 0x00657a69u,
    0x00030001u, 0x00040001u, 0x00000001u, 0x00000000u, 0x325f7370u, 0x4d00305fu,
    0x6f726369u, 0x74666f73u, 0x29522820u, 0x534c4820u, 0x6853204cu, 0x72656461u,
    0x6d6f4320u, 0x656c6970u, 0x30312072u, 0xab00312eu, 0x05000051u, 0xa00f0001u,
    0xbf000000u, 0x3f000000u, 0x3f800000u, 0x40200000u, 0x05000051u, 0xa00f0002u,
    0x3fc00000u, 0xc0200000u, 0x40000000u, 0x00000000u, 0x0200001fu, 0x80000000u,
    0xb0030000u, 0x0200001fu, 0x90000000u, 0xa00f0800u, 0x02000001u, 0x80080000u,
    0xa0000001u, 0x04000004u, 0x80030000u, 0xb0e40000u, 0xa0e40000u, 0x80ff0000u,
    0x02000013u, 0x800c0000u, 0x801b0000u, 0x03000002u, 0x80030000u, 0x811b0000u,
    0x80e40000u, 0x03000002u, 0x800c0000u, 0x801b0000u, 0xa0000001u, 0x02000006u,
    0x80010001u, 0xa0000000u, 0x02000006u, 0x80020001u, 0xa0550000u, 0x03000005u,
    0x80030002u, 0x801b0000u, 0x80e40001u, 0x02000001u, 0x80010003u, 0x80000002u,
    0x03000002u, 0x800c0000u, 0x801b0000u, 0xa0550001u, 0x03000002u, 0x80030000u,
    0x80e40000u, 0xa0ff0001u, 0x03000005u, 0x80030000u, 0x80e40001u, 0x80e40000u,
    0x04000004u, 0x800c0001u, 0xb01b0000u, 0xa01b0000u, 0x81e40000u, 0x04000004u,
    0x800c0002u, 0x80e40001u, 0xa1000002u, 0xa0aa0002u, 0x04000004u, 0x800c0002u,
    0x80e40001u, 0x80e40002u, 0xa0550001u, 0x03000005u, 0x800c0003u, 0x80e40001u,
    0x80e40002u, 0x04000004u, 0x80030004u, 0x801b0001u, 0xa0000002u, 0xa0550002u,
    0x03000005u, 0x800c0004u, 0x80e40001u, 0x80e40001u, 0x04000004u, 0x80030004u,
    0x801b0004u, 0x80e40004u, 0xa0aa0001u, 0x04000004u, 0x800c0002u, 0x80e40001u,
    0x80e40002u, 0x801b0004u, 0x02000006u, 0x80010004u, 0x80ff0002u, 0x02000006u,
    0x80020004u, 0x80aa0002u, 0x04000004u, 0x800c0000u, 0x80e40003u, 0x801b0004u,
    0x80e40000u, 0x03000005u, 0x80030001u, 0x80e40001u, 0x801b0000u, 0x02000001u,
    0x80020003u, 0x80550001u, 0x02000001u, 0x80020004u, 0x80550003u, 0x02000001u,
    0x80010002u, 0x80000001u, 0x02000001u, 0x80010005u, 0x80000002u, 0x02000001u,
    0x80010004u, 0x80000000u, 0x02000001u, 0x80020005u, 0x80550000u, 0x03000042u,
    0x800f0000u, 0x80e40003u, 0xa0e40800u, 0x03000042u, 0x800f0003u, 0x80e40001u,
    0xa0e40800u, 0x03000042u, 0x800f0006u, 0x80e40002u, 0xa0e40800u, 0x03000042u,
    0x800f0005u, 0x80e40005u, 0xa0e40800u, 0x03000042u, 0x800f0007u, 0x80e40004u,
    0xa0e40800u, 0x04000004u, 0x80030001u, 0x801b0001u, 0xa1550001u, 0xa0aa0001u,
    0x04000004u, 0x80030001u, 0x801b0001u, 0x80e40001u, 0xa0000001u, 0x03000005u,
    0x80030001u, 0x80e40001u, 0x801b0001u, 0x04000004u, 0x800c0001u, 0x80e40001u,
    0xa0550001u, 0xa0000001u, 0x03000005u, 0x800c0001u, 0x80e40001u, 0x80e40004u,
    0x03000005u, 0x80080000u, 0x80aa0002u, 0x80000001u, 0x03000005u, 0x80070000u,
    0x80ff0000u, 0x80e40000u, 0x04000004u, 0x80080000u, 0x80ff0002u, 0x80550001u,
    0x80ff0000u, 0x03000005u, 0x80080003u, 0x80550001u, 0x80ff0002u, 0x04000004u,
    0x80080000u, 0x80ff0002u, 0x80aa0002u, 0x80ff0000u, 0x04000004u, 0x80080000u,
    0x80ff0001u, 0x80aa0002u, 0x80ff0000u, 0x04000004u, 0x80080000u, 0x80ff0002u,
    0x80aa0001u, 0x80ff0000u, 0x02000006u, 0x80080000u, 0x80ff0000u, 0x04000004u,
    0x80070000u, 0x80e40006u, 0x80ff0003u, 0x80e40000u, 0x03000005u, 0x80080003u,
    0x80aa0002u, 0x80ff0002u, 0x04000004u, 0x80070000u, 0x80e40003u, 0x80ff0003u,
    0x80e40000u, 0x03000005u, 0x80080005u, 0x80aa0002u, 0x80ff0001u, 0x03000005u,
    0x80080007u, 0x80aa0001u, 0x80ff0002u, 0x04000004u, 0x80070000u, 0x80e40007u,
    0x80ff0005u, 0x80e40000u, 0x04000004u, 0x80070000u, 0x80e40005u, 0x80ff0007u,
    0x80e40000u, 0x03000005u, 0x80070000u, 0x80ff0000u, 0x80e40000u, 0x02000001u,
    0x80080000u, 0xa0aa0001u, 0x02000001u, 0x800f0800u, 0x80e40000u, 0x0000ffffu,
}};

constexpr u64 D3D9CatmullRomShaderFnv1a64() {
    u64 hash = 14695981039346656037ull;
    for (const u32 word : kD3D9CatmullRomPixelShader) {
        for (u32 shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<u8>((word >> shift) & 0xffu);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

static_assert(sizeof(kD3D9CatmullRomPixelShader) == 1200,
    "cnc-ddraw Catmull-Rom bytecode size changed");
static_assert(D3D9CatmullRomShaderFnv1a64() == 0x4dd0edd1b1e752d6ull,
    "cnc-ddraw Catmull-Rom bytecode changed");

} // namespace ranker
