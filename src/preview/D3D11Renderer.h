#pragma once

#include "core/Frame.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>

namespace frameforge {

using Microsoft::WRL::ComPtr;

// ============================================================
// Minimal D3D11 preview renderer.
//
// - Owns a flip-model swap chain bound to an HWND.
// - Uploads NV12 (Y + interleaved UV planes) or BGRA frames
//   straight into GPU textures (no CPU color conversion).
// - Converts NV12 -> RGB in a pixel shader.
// - Letterboxes the video inside the client area.
//
// All methods must be called from the thread that created the
// window/renderer.
// ============================================================
class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    // Create device, swap chain and pipeline state for the given window.
    HRESULT Initialize(HWND hwnd, UINT width, UINT height);

    // Recreate swap-chain buffers after a resize (0x0 is ignored).
    HRESULT Resize(UINT width, UINT height);

    // Upload and draw one frame. Present() must be called to display it.
    HRESULT Render(const Frame& frame);

    // Present the last rendered content (vsync paced).
    HRESULT Present();

    // Diagnostic helper: copy the current back buffer to CPU memory as
    // tightly packed BGRA (width * height * 4 bytes). Used by tests to
    // verify the NV12 -> RGB conversion without relying on screenshots.
    HRESULT ReadBackPixels(std::vector<uint8_t>& pixels,
                           UINT& width, UINT& height);

    void Shutdown();

    bool IsInitialized() const { return m_swapChain != nullptr; }
    UINT BackBufferWidth() const { return m_backBufferWidth; }
    UINT BackBufferHeight() const { return m_backBufferHeight; }

private:
    HRESULT CreateDeviceAndSwapChain(HWND hwnd, UINT width, UINT height);
    HRESULT CreateRenderTarget();
    HRESULT CreatePipeline();
    HRESULT EnsureTextures(const Frame& frame);
    HRESULT UploadNv12(const Frame& frame);
    HRESULT UploadBgra(const Frame& frame);

    UINT m_backBufferWidth  = 0;
    UINT m_backBufferHeight = 0;

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain1>        m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTarget;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader>  m_nv12PixelShader;
    ComPtr<ID3D11PixelShader>  m_bgraPixelShader;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_rasterizer;

    // NV12 textures (R8 luma + R8G8 chroma)
    ComPtr<ID3D11Texture2D>            m_textureY;
    ComPtr<ID3D11ShaderResourceView>   m_viewY;
    ComPtr<ID3D11Texture2D>            m_textureUV;
    ComPtr<ID3D11ShaderResourceView>   m_viewUV;

    // BGRA texture fallback
    ComPtr<ID3D11Texture2D>            m_textureBgra;
    ComPtr<ID3D11ShaderResourceView>   m_viewBgra;

    UINT m_textureWidth  = 0;
    UINT m_textureHeight = 0;
    FrameFormat m_textureFormat = FrameFormat::NV12;
};

} // namespace frameforge
