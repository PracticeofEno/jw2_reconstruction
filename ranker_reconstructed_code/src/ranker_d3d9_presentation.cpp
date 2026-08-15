#include "ranker_d3d9_presentation.h"

#ifdef _WIN32
#include "ranker_d3d9_cubic_contract.h"
#include "ranker_ddraw_ini.h"

#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

namespace ranker {
namespace {

struct D3D9CubicPresentationState {
    HWND window = nullptr;
    DWORD owner_thread_id = 0;
    IDirect3D9* api = nullptr;
    IDirect3DDevice9* device = nullptr;
    std::array<IDirect3DTexture9*, kD3D9CubicTextureCount> textures{};
    IDirect3DTexture9* cursor_texture = nullptr;
    IDirect3DVertexBuffer9* vertex_buffer = nullptr;
    IDirect3DPixelShader9* pixel_shader = nullptr;
    D3DPRESENT_PARAMETERS parameters{};
    u32 output_width = 0;
    u32 output_height = 0;
    u32 next_texture_index = 0;
    u32 last_texture_index = 0;
    u32 generation = 0;
    DWORD lifecycle_retry_after = 0;
    u32 consecutive_lifecycle_failures = 0;
    bool requested = false;
    bool active = false;
    bool resources_ready = false;
    bool owner_request_posted = false;
    bool lifecycle_in_progress = false;
    bool present_in_progress = false;
    bool uploaded_background_valid = false;
    D3D9CubicPresentationTelemetry telemetry{};
};

std::recursive_mutex g_cubic_mutex;
D3D9CubicPresentationState g_cubic;
u32 g_generation_seed = 0;

template <typename T>
void release_com(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

u32 next_generation() {
    ++g_generation_seed;
    if (g_generation_seed == 0) {
        ++g_generation_seed;
    }
    return g_generation_seed;
}

std::string normalized_ini_value(const char* key, const char* fallback) {
    std::array<char, 64> value{};
    GetPrivateProfileStringA("ddraw", key, fallback, value.data(),
        static_cast<DWORD>(value.size()), RankerDdrawIniPath().c_str());
    std::string normalized(value.data());
    normalized.erase(normalized.begin(), std::find_if(normalized.begin(),
        normalized.end(), [](unsigned char character) {
            return !std::isspace(character);
        }));
    normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(),
        [](unsigned char character) {
            return !std::isspace(character);
        }).base(), normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return normalized;
}

u32 configured_d3d9_filter() {
    return static_cast<u32>(GetPrivateProfileIntA("ddraw", "d3d9_filter",
        kD3D9FilterCubic, RankerDdrawIniPath().c_str()));
}

bool configured_renderer_supported() {
    const std::string renderer = normalized_ini_value("renderer", "auto");
    return IsD3D9CubicRendererSupported(renderer);
}

bool configured_boolean(const char* key, bool fallback) {
    const std::string value = normalized_ini_value(
        key, fallback ? "true" : "false");
    if (value == "true" || value == "yes" || value == "on" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "off" || value == "0") {
        return false;
    }
    return fallback;
}

bool tick_precedes(DWORD now, DWORD deadline) {
    return deadline != 0 && static_cast<LONG>(now - deadline) < 0;
}

void release_resources_locked() {
    g_cubic.active = false;
    g_cubic.resources_ready = false;
    const bool resources_exist = g_cubic.pixel_shader != nullptr ||
        g_cubic.cursor_texture != nullptr ||
        g_cubic.vertex_buffer != nullptr ||
        std::any_of(g_cubic.textures.begin(), g_cubic.textures.end(),
            [](IDirect3DTexture9* texture) { return texture != nullptr; });
    // A failed Reset leaves the device in a state where only Reset,
    // TestCooperativeLevel and Release are valid.  At that point our resource
    // pointers are already null, so do not issue another SetTexture call.
    if (g_cubic.device != nullptr && resources_exist) {
        g_cubic.device->SetTexture(0, nullptr);
    }
    release_com(g_cubic.pixel_shader);
    release_com(g_cubic.cursor_texture);
    release_com(g_cubic.vertex_buffer);
    for (auto*& texture : g_cubic.textures) {
        release_com(texture);
    }
    g_cubic.next_texture_index = 0;
    g_cubic.last_texture_index = 0;
    g_cubic.uploaded_background_valid = false;
}

void shutdown_locked() {
    g_cubic.requested = false;
    g_cubic.active = false;
    g_cubic.owner_request_posted = false;
    release_resources_locked();
    release_com(g_cubic.device);
    release_com(g_cubic.api);
    g_cubic = D3D9CubicPresentationState{};
}

bool post_owner_request_locked() {
    if (!g_cubic.requested || g_cubic.window == nullptr ||
        g_cubic.owner_request_posted || g_cubic.lifecycle_in_progress ||
        g_cubic.present_in_progress) {
        return false;
    }
    const DWORD now = GetTickCount();
    if (tick_precedes(now, g_cubic.lifecycle_retry_after)) {
        return false;
    }
    if (!PostMessageA(g_cubic.window,
            kD3D9CubicPresentationOwnerThreadMessage,
            static_cast<WPARAM>(g_cubic.generation), 0)) {
        const DWORD error = GetLastError();
        g_cubic.telemetry.last_lifecycle_result = error != ERROR_SUCCESS ?
            HRESULT_FROM_WIN32(error) : E_FAIL;
        return false;
    }
    g_cubic.owner_request_posted = true;
    return true;
}

void record_lifecycle_failure_locked(HRESULT result) {
    g_cubic.active = false;
    g_cubic.telemetry.last_lifecycle_result = result;
    ++g_cubic.telemetry.lifecycle_failure_count;
    ++g_cubic.consecutive_lifecycle_failures;
    const u32 shift = std::min<u32>(g_cubic.consecutive_lifecycle_failures - 1, 4);
    const DWORD delay = std::min<DWORD>(250u << shift, 4000u);
    g_cubic.lifecycle_retry_after = GetTickCount() + delay;
}

void record_lifecycle_success_locked() {
    g_cubic.telemetry.last_lifecycle_result = D3D_OK;
    g_cubic.consecutive_lifecycle_failures = 0;
    g_cubic.lifecycle_retry_after = 0;
}

HRESULT fallback_locked(HRESULT result, bool request_lifecycle) {
    // Publish the inactive state before the DirectDraw caller can enter its
    // primary-surface cursor path, and before scheduling recovery.
    g_cubic.active = false;
    g_cubic.telemetry.last_present_result = result;
    ++g_cubic.telemetry.fallback_present_count;
    if (request_lifecycle) {
        post_owner_request_locked();
    }
    return result;
}

D3DPRESENT_PARAMETERS make_present_parameters(u32 width, u32 height) {
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = width;
    parameters.BackBufferHeight = height;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = g_cubic.window;
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    return parameters;
}

HRESULT update_vertices_locked(u32 width, u32 height) {
    if (g_cubic.vertex_buffer == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    void* destination = nullptr;
    HRESULT result = g_cubic.vertex_buffer->Lock(0, 0, &destination, 0);
    if (FAILED(result)) {
        return result;
    }
    const auto vertices = BuildD3D9CubicVertices(width, height);
    std::memcpy(destination, vertices.data(), sizeof(vertices));
    result = g_cubic.vertex_buffer->Unlock();
    return result;
}

HRESULT apply_render_state_locked(u32 width, u32 height) {
    if (g_cubic.device == nullptr || g_cubic.vertex_buffer == nullptr ||
        g_cubic.pixel_shader == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    HRESULT result = g_cubic.device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetStreamSource(
            0, g_cubic.vertex_buffer, 0, sizeof(D3D9CubicVertex));
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetPixelShader(g_cubic.pixel_shader);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetSamplerState(
            0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetSamplerState(
            0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetRenderState(D3DRS_ZENABLE, FALSE);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
    if (SUCCEEDED(result)) {
        const float texture_size[4] = {
            static_cast<float>(kD3D9CubicTextureWidth),
            static_cast<float>(kD3D9CubicTextureHeight), 0.0f, 0.0f};
        result = g_cubic.device->SetPixelShaderConstantF(0, texture_size, 1);
    }
    if (SUCCEEDED(result)) {
        const D3DVIEWPORT9 viewport{0, 0, width, height, 0.0f, 1.0f};
        result = g_cubic.device->SetViewport(&viewport);
    }
    return result;
}

HRESULT clear_texture_locked(IDirect3DTexture9* texture) {
    if (texture == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    D3DLOCKED_RECT locked{};
    HRESULT result = texture->LockRect(0, &locked, nullptr, 0);
    if (FAILED(result)) {
        return result;
    }
    for (u32 y = 0; y < kD3D9CubicTextureHeight; ++y) {
        std::memset(static_cast<u8*>(locked.pBits) +
                static_cast<std::size_t>(y) * locked.Pitch,
            0, kD3D9CubicTextureWidth * sizeof(u16));
    }
    return texture->UnlockRect(0);
}

HRESULT create_resources_locked(u32 width, u32 height) {
    release_resources_locked();
    HRESULT result = g_cubic.device->CreateVertexBuffer(
        sizeof(D3D9CubicVertex) * 4, 0, D3DFVF_XYZRHW | D3DFVF_TEX1,
        D3DPOOL_MANAGED, &g_cubic.vertex_buffer, nullptr);
    for (auto*& texture : g_cubic.textures) {
        if (FAILED(result)) {
            break;
        }
        result = g_cubic.device->CreateTexture(kD3D9CubicTextureWidth,
            kD3D9CubicTextureHeight, 1, 0, D3DFMT_R5G6B5, D3DPOOL_MANAGED,
            &texture, nullptr);
        if (SUCCEEDED(result)) {
            result = clear_texture_locked(texture);
        }
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->CreateTexture(kD3D9CursorTextureSize,
            kD3D9CursorTextureSize, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &g_cubic.cursor_texture, nullptr);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(kD3D9CatmullRomPixelShader.data()),
            &g_cubic.pixel_shader);
    }
    if (SUCCEEDED(result)) {
        result = update_vertices_locked(width, height);
    }
    if (SUCCEEDED(result)) {
        result = apply_render_state_locked(width, height);
    }
    if (FAILED(result)) {
        release_resources_locked();
        return result;
    }
    g_cubic.resources_ready = true;
    return D3D_OK;
}

HRESULT create_device_owner_locked(u32 width, u32 height) {
    if (GetCurrentThreadId() != g_cubic.owner_thread_id) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
    }
    release_com(g_cubic.api);
    g_cubic.api = Direct3DCreate9(D3D_SDK_VERSION);
    if (g_cubic.api == nullptr) {
        return D3DERR_NOTAVAILABLE;
    }
    g_cubic.parameters = make_present_parameters(width, height);
    constexpr std::array<DWORD, 2> behavior_flags{{
        D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE |
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
        D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE |
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
    }};
    HRESULT result = D3DERR_NOTAVAILABLE;
    for (const DWORD flags : behavior_flags) {
        result = g_cubic.api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            g_cubic.window, flags, &g_cubic.parameters, &g_cubic.device);
        if (SUCCEEDED(result)) {
            break;
        }
    }
    if (FAILED(result)) {
        release_com(g_cubic.api);
        return result;
    }
    result = create_resources_locked(width, height);
    if (FAILED(result)) {
        release_com(g_cubic.device);
        release_com(g_cubic.api);
        return result;
    }
    g_cubic.output_width = width;
    g_cubic.output_height = height;
    return D3D_OK;
}

HRESULT reset_device_owner_locked(u32 width, u32 height) {
    if (GetCurrentThreadId() != g_cubic.owner_thread_id) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
    }
    release_resources_locked();
    g_cubic.parameters = make_present_parameters(width, height);
    HRESULT result = g_cubic.device->Reset(&g_cubic.parameters);
    if (FAILED(result)) {
        return result;
    }
    result = create_resources_locked(width, height);
    if (SUCCEEDED(result)) {
        g_cubic.output_width = width;
        g_cubic.output_height = height;
    }
    return result;
}

HRESULT ensure_device_owner_locked(u32 width, u32 height) {
    if (GetCurrentThreadId() != g_cubic.owner_thread_id) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
    }
    if (g_cubic.device == nullptr) {
        return create_device_owner_locked(width, height);
    }
    const HRESULT cooperative = g_cubic.device->TestCooperativeLevel();
    if (cooperative == D3DERR_DEVICELOST) {
        return cooperative;
    }
    if (cooperative == D3DERR_DEVICENOTRESET || !g_cubic.resources_ready ||
        width != g_cubic.output_width || height != g_cubic.output_height) {
        return reset_device_owner_locked(width, height);
    }
    return cooperative;
}

HRESULT upload_back_buffer_locked(LPDIRECTDRAWSURFACE7 back_surface,
    IDirect3DTexture9* texture) {
    DDSURFACEDESC2 source{};
    source.dwSize = sizeof(source);
    HRESULT result = back_surface->Lock(
        nullptr, &source, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    if (FAILED(result)) {
        return result;
    }
    const LONG minimum_pitch = static_cast<LONG>(
        kD3D9CubicLogicalWidth * sizeof(u16));
    if (source.lpSurface == nullptr || source.lPitch < minimum_pitch ||
        source.dwWidth < kD3D9CubicLogicalWidth ||
        source.dwHeight < kD3D9CubicLogicalHeight) {
        back_surface->Unlock(nullptr);
        return DDERR_INVALIDPIXELFORMAT;
    }
    const RECT destination_rect{0, 0,
        static_cast<LONG>(kD3D9CubicLogicalWidth),
        static_cast<LONG>(kD3D9CubicLogicalHeight)};
    D3DLOCKED_RECT destination{};
    result = texture->LockRect(0, &destination, &destination_rect, 0);
    if (SUCCEEDED(result)) {
        const std::size_t row_bytes = kD3D9CubicLogicalWidth * sizeof(u16);
        for (u32 y = 0; y < kD3D9CubicLogicalHeight; ++y) {
            std::memcpy(static_cast<u8*>(destination.pBits) +
                    static_cast<std::size_t>(y) * destination.Pitch,
                static_cast<const u8*>(source.lpSurface) +
                    static_cast<std::size_t>(y) * source.lPitch,
                row_bytes);
        }
        const HRESULT unlock_result = texture->UnlockRect(0);
        if (FAILED(unlock_result)) {
            result = unlock_result;
        }
    }
    const HRESULT source_unlock = back_surface->Unlock(nullptr);
    return FAILED(result) ? result : source_unlock;
}

HRESULT upload_cursor_locked(LPDIRECTDRAWSURFACE7 cursor_surface) {
    if (cursor_surface == nullptr || g_cubic.cursor_texture == nullptr) {
        return E_POINTER;
    }

    DDSURFACEDESC2 source{};
    source.dwSize = sizeof(source);
    HRESULT result = cursor_surface->Lock(
        nullptr, &source, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    if (FAILED(result)) {
        return result;
    }
    if (source.lpSurface == nullptr ||
        source.lPitch < static_cast<LONG>(kD3D9CursorTextureSize * sizeof(u16)) ||
        source.dwWidth < kD3D9CursorTextureSize ||
        source.dwHeight < kD3D9CursorTextureSize) {
        cursor_surface->Unlock(nullptr);
        return DDERR_INVALIDPIXELFORMAT;
    }

    D3DLOCKED_RECT destination{};
    result = g_cubic.cursor_texture->LockRect(0, &destination, nullptr, 0);
    if (SUCCEEDED(result)) {
        for (u32 y = 0; y < kD3D9CursorTextureSize; ++y) {
            const auto* source_row = reinterpret_cast<const u16*>(
                static_cast<const u8*>(source.lpSurface) +
                static_cast<std::size_t>(y) * source.lPitch);
            auto* destination_row = reinterpret_cast<u32*>(
                static_cast<u8*>(destination.pBits) +
                static_cast<std::size_t>(y) * destination.Pitch);
            for (u32 x = 0; x < kD3D9CursorTextureSize; ++x) {
                const u16 pixel = source_row[x];
                if (pixel == 0) {
                    destination_row[x] = 0;
                    continue;
                }
                const u32 red5 = (pixel >> 11) & 0x1fu;
                const u32 green6 = (pixel >> 5) & 0x3fu;
                const u32 blue5 = pixel & 0x1fu;
                const u32 red8 = (red5 << 3) | (red5 >> 2);
                const u32 green8 = (green6 << 2) | (green6 >> 4);
                const u32 blue8 = (blue5 << 3) | (blue5 >> 2);
                destination_row[x] = 0xff000000u |
                    (red8 << 16) | (green8 << 8) | blue8;
            }
        }
        const HRESULT unlock_result = g_cubic.cursor_texture->UnlockRect(0);
        if (FAILED(unlock_result)) {
            result = unlock_result;
        }
    }
    const HRESULT source_unlock = cursor_surface->Unlock(nullptr);
    return FAILED(result) ? result : source_unlock;
}

HRESULT apply_cursor_render_state_locked() {
    HRESULT result = g_cubic.device->SetPixelShader(nullptr);
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetTexture(0, g_cubic.cursor_texture);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetSamplerState(
            0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetSamplerState(
            0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetTextureStageState(
            0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetTextureStageState(
            0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetTextureStageState(
            0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetTextureStageState(
            0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetRenderState(
            D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }
    return result;
}

HRESULT draw_and_present_locked(
    const D3D9CursorOverlayGeometry* cursor_geometry) {
    HRESULT result = apply_render_state_locked(
        g_cubic.output_width, g_cubic.output_height);
    if (FAILED(result)) {
        return result;
    }
    result = g_cubic.device->BeginScene();
    if (FAILED(result)) {
        return result;
    }
    result = g_cubic.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    if (SUCCEEDED(result) && cursor_geometry != nullptr &&
        cursor_geometry->visible) {
        result = apply_cursor_render_state_locked();
        if (SUCCEEDED(result)) {
            result = g_cubic.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2,
                cursor_geometry->vertices.data(), sizeof(D3D9CubicVertex));
        }
    }
    const HRESULT end_result = g_cubic.device->EndScene();
    if (FAILED(result)) {
        return result;
    }
    if (FAILED(end_result)) {
        return end_result;
    }
    return g_cubic.device->Present(nullptr, nullptr, nullptr, nullptr);
}

HRESULT try_present_locked(LPDIRECTDRAWSURFACE7 back_surface,
    LPDIRECTDRAWSURFACE7 cursor_surface, i32 cursor_x, i32 cursor_y,
    i32 hotspot_x, i32 hotspot_y, bool reuse_uploaded_background) {
    if (!g_cubic.requested) {
        g_cubic.active = false;
        return S_FALSE;
    }
    if (back_surface == nullptr || g_cubic.window == nullptr) {
        return fallback_locked(E_POINTER, false);
    }
    if (g_cubic.lifecycle_in_progress || g_cubic.present_in_progress) {
        return fallback_locked(S_FALSE, false);
    }
    RECT client{};
    if (!GetClientRect(g_cubic.window, &client)) {
        const DWORD error = GetLastError();
        return fallback_locked(error != ERROR_SUCCESS ?
            HRESULT_FROM_WIN32(error) : E_FAIL, true);
    }
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return fallback_locked(D3DERR_DEVICELOST, true);
    }
    if (g_cubic.device == nullptr || !g_cubic.resources_ready ||
        static_cast<u32>(width) != g_cubic.output_width ||
        static_cast<u32>(height) != g_cubic.output_height) {
        return fallback_locked(S_FALSE, true);
    }
    if (MayProbeD3D9CooperativeLevel(GetCurrentThreadId(),
            g_cubic.owner_thread_id)) {
        const HRESULT cooperative = g_cubic.device->TestCooperativeLevel();
        if (cooperative != D3D_OK) {
            return fallback_locked(cooperative, true);
        }
    }

    D3D9CursorOverlayGeometry cursor_geometry{};
    const D3D9CursorOverlayGeometry* cursor_geometry_pointer = nullptr;
    if (cursor_surface != nullptr) {
        cursor_geometry = BuildD3D9CursorOverlayGeometry(
            g_cubic.output_width, g_cubic.output_height, cursor_x, cursor_y,
            hotspot_x, hotspot_y);
        if (cursor_geometry.visible) {
            cursor_geometry_pointer = &cursor_geometry;
        }
    }

    g_cubic.present_in_progress = true;
    const bool reuse_background = reuse_uploaded_background &&
        g_cubic.uploaded_background_valid;
    const u32 texture_index = reuse_background ?
        g_cubic.last_texture_index : g_cubic.next_texture_index;
    IDirect3DTexture9* texture = g_cubic.textures[texture_index];
    HRESULT result = D3D_OK;
    if (!reuse_background) {
        result = upload_back_buffer_locked(back_surface, texture);
    }
    if (SUCCEEDED(result) && cursor_geometry_pointer != nullptr) {
        result = upload_cursor_locked(cursor_surface);
    }
    if (SUCCEEDED(result)) {
        result = g_cubic.device->SetTexture(0, texture);
    }
    if (SUCCEEDED(result)) {
        result = draw_and_present_locked(cursor_geometry_pointer);
    }
    g_cubic.present_in_progress = false;
    if (result != D3D_OK) {
        return fallback_locked(result, true);
    }
    if (!reuse_background) {
        g_cubic.last_texture_index = texture_index;
        g_cubic.uploaded_background_valid = true;
        g_cubic.next_texture_index =
            (texture_index + 1) % kD3D9CubicTextureCount;
    }
    g_cubic.active = true;
    g_cubic.telemetry.last_present_result = D3D_OK;
    ++g_cubic.telemetry.successful_present_count;
    return D3D_OK;
}

} // namespace

void ConfigureD3D9CubicPresentation(HWND window, u32 logical_width,
    u32 logical_height, u32 color_depth, bool windowed, u32 red_mask,
    u32 green_mask, u32 blue_mask) {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    shutdown_locked();
    g_cubic.generation = next_generation();
    g_cubic.window = window;
    g_cubic.owner_thread_id = window != nullptr ?
        GetWindowThreadProcessId(window, nullptr) : 0;
    g_cubic.requested = g_cubic.owner_thread_id != 0 &&
        ShouldUseD3D9CubicPresentation(sizeof(void*) == 8, windowed,
            configured_renderer_supported(), configured_boolean("boxing", false),
            configured_d3d9_filter(), logical_width, logical_height, color_depth,
            red_mask, green_mask, blue_mask);
    if (g_cubic.requested) {
        // Always asynchronous: CreateDevice/Reset can synchronously re-enter
        // this window procedure while the recursive lifecycle guard is set.
        post_owner_request_locked();
    }
}

void ShutdownD3D9CubicPresentation() {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    shutdown_locked();
    next_generation();
}

bool IsD3D9CubicPresentationRequested() {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    return g_cubic.requested;
}

bool IsD3D9CubicPresentationActive() {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    return g_cubic.requested && g_cubic.active && g_cubic.resources_ready &&
        g_cubic.device != nullptr;
}

bool HandleD3D9CubicPresentationOwnerThreadRequest(
    HWND window, WPARAM generation) {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    if (window != g_cubic.window ||
        static_cast<u32>(generation) != g_cubic.generation) {
        return false;
    }
    g_cubic.owner_request_posted = false;
    if (!g_cubic.requested || GetCurrentThreadId() != g_cubic.owner_thread_id ||
        g_cubic.lifecycle_in_progress || g_cubic.present_in_progress) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        const DWORD error = GetLastError();
        record_lifecycle_failure_locked(error != ERROR_SUCCESS ?
            HRESULT_FROM_WIN32(error) : E_FAIL);
        return false;
    }
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        record_lifecycle_failure_locked(D3DERR_DEVICELOST);
        return false;
    }
    g_cubic.active = false;
    g_cubic.lifecycle_in_progress = true;
    const HRESULT result = ensure_device_owner_locked(
        static_cast<u32>(width), static_cast<u32>(height));
    g_cubic.lifecycle_in_progress = false;
    if (FAILED(result)) {
        record_lifecycle_failure_locked(result);
        return false;
    }
    record_lifecycle_success_locked();
    return true;
}

D3D9CubicPresentationTelemetry GetD3D9CubicPresentationTelemetry() {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    return g_cubic.telemetry;
}

HRESULT TryPresentBackBufferWithD3D9Cubic(
    LPDIRECTDRAWSURFACE7 back_surface) {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    return try_present_locked(back_surface, nullptr, 0, 0, 0, 0, false);
}

HRESULT TryPresentBackBufferWithD3D9CubicCursor(
    LPDIRECTDRAWSURFACE7 back_surface,
    LPDIRECTDRAWSURFACE7 cursor_surface,
    i32 cursor_x, i32 cursor_y, i32 hotspot_x, i32 hotspot_y,
    bool reuse_uploaded_background) {
    std::lock_guard<std::recursive_mutex> lock(g_cubic_mutex);
    return try_present_locked(back_surface, cursor_surface,
        cursor_x, cursor_y, hotspot_x, hotspot_y, reuse_uploaded_background);
}

} // namespace ranker
#endif
