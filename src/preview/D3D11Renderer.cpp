#include "preview/D3D11Renderer.h"

#include "core/common.h"
#include "diagnostics/Logger.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>

namespace frameforge {

namespace {

// Full-screen triangle-strip quad generated from SV_VertexID.
// UV origin is top-left, matching Media Foundation frame layout.
constexpr const char* kShaderSource = R"(
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    float2 positions[4] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };
    float2 uvs[4] = {
        float2(0.0, 1.0),
        float2(1.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 0.0)
    };

    VSOut o;
    o.pos = float4(positions[id], 0.0, 1.0);
    o.uv  = uvs[id];
    return o;
}

Texture2D<float>  texY  : register(t0);
Texture2D<float2> texUV : register(t1);
Texture2D<float4> texRGBA : register(t0);
SamplerState samp : register(s0);

float4 PSNv12(VSOut i) : SV_Target {
    float y = texY.Sample(samp, i.uv).r;
    float2 uv = texUV.Sample(samp, i.uv).rg;

    // BT.601 limited range (16..235 luma, 16..240 chroma)
    y = saturate((y - 16.0 / 255.0) * (255.0 / 219.0));
    uv = saturate((uv - (16.0 / 255.0)) * (255.0 / 224.0)) - 0.5;

    float r = y + 1.402 * uv.y;
    float g = y - 0.344136 * uv.x - 0.714136 * uv.y;
    float b = y + 1.772 * uv.x;

    return float4(saturate(float3(r, g, b)), 1.0);
}

float4 PSBgra(VSOut i) : SV_Target {
    return float4(texRGBA.Sample(samp, i.uv).rgb, 1.0);
}
)";

HRESULT CreateDynamicTexture(
    ID3D11Device* device,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    ID3D11Texture2D** outTexture,
    ID3D11ShaderResourceView** outView) {

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width              = width;
    desc.Height             = height;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = format;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage              = D3D11_USAGE_DYNAMIC;
    desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags          = 0;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, outTexture);
    if (FAILED(hr)) return hr;

    hr = device->CreateShaderResourceView(*outTexture, nullptr, outView);
    if (FAILED(hr)) {
        (*outTexture)->Release();
        *outTexture = nullptr;
    }
    return hr;
}

} // anonymous namespace

D3D11Renderer::~D3D11Renderer() {
    Shutdown();
}

HRESULT D3D11Renderer::Initialize(HWND hwnd, UINT width, UINT height) {
    Shutdown();

    if (!hwnd) {
        return E_INVALIDARG;
    }

    HRESULT hr = CreateDeviceAndSwapChain(hwnd, width, height);
    if (FAILED(hr)) {
        Shutdown();
        return hr;
    }

    hr = CreateRenderTarget();
    if (FAILED(hr)) {
        Shutdown();
        return hr;
    }

    hr = CreatePipeline();
    if (FAILED(hr)) {
        Shutdown();
        return hr;
    }

    Logger::Global().Info("D3D11Renderer",
        std::format("D3D11 preview initialized ({}x{})", width, height));
    return S_OK;
}

HRESULT D3D11Renderer::CreateDeviceAndSwapChain(
    HWND hwnd, UINT width, UINT height) {

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_10_0;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        &m_device,
        &obtained,
        &m_context);

    if (FAILED(hr)) {
        Logger::Global().Warn("D3D11Renderer",
            std::format("Hardware device creation failed ({}), trying WARP",
                HResultToString(hr)));
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &m_device,
            &obtained,
            &m_context);
    }

    if (FAILED(hr)) {
        Logger::Global().Error("D3D11Renderer",
            std::format("D3D11CreateDevice failed: {}", HResultToString(hr)));
        return hr;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width              = width > 0 ? width : 1;
    desc.Height             = height > 0 ? height : 1;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo             = FALSE;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount        = 2;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags              = 0;

    hr = factory->CreateSwapChainForHwnd(
        m_device.Get(), hwnd, &desc, nullptr, nullptr, &m_swapChain);
    if (FAILED(hr)) {
        Logger::Global().Error("D3D11Renderer",
            std::format("CreateSwapChainForHwnd failed: {}",
                HResultToString(hr)));
        return hr;
    }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_backBufferWidth  = desc.Width;
    m_backBufferHeight = desc.Height;

    Logger::Global().Debug("D3D11Renderer",
        std::format("D3D11 device created (feature level 0x{:X})",
            static_cast<unsigned>(obtained)));
    return S_OK;
}

HRESULT D3D11Renderer::CreateRenderTarget() {
    m_renderTarget.Reset();

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return hr;

    return m_device->CreateRenderTargetView(
        backBuffer.Get(), nullptr, &m_renderTarget);
}

HRESULT D3D11Renderer::CreatePipeline() {
    ComPtr<ID3DBlob> vsBlob, psNv12Blob, psBgraBlob, errorBlob;

    auto compile = [&](const char* entry, const char* target,
                       ComPtr<ID3DBlob>& blob) -> HRESULT {
        errorBlob.Reset();
        HRESULT hr = D3DCompile(
            kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &blob, &errorBlob);
        if (FAILED(hr) && errorBlob) {
            Logger::Global().Error("D3D11Renderer",
                std::format("Shader '{}' compilation failed: {}",
                    entry,
                    static_cast<const char*>(errorBlob->GetBufferPointer())));
        }
        return hr;
    };

    HRESULT hr = compile("VSMain", "vs_4_0", vsBlob);
    if (FAILED(hr)) return hr;
    hr = compile("PSNv12", "ps_4_0", psNv12Blob);
    if (FAILED(hr)) return hr;
    hr = compile("PSBgra", "ps_4_0", psBgraBlob);
    if (FAILED(hr)) return hr;

    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, &m_vertexShader);
    if (FAILED(hr)) return hr;

    hr = m_device->CreatePixelShader(
        psNv12Blob->GetBufferPointer(), psNv12Blob->GetBufferSize(),
        nullptr, &m_nv12PixelShader);
    if (FAILED(hr)) return hr;

    hr = m_device->CreatePixelShader(
        psBgraBlob->GetBufferPointer(), psBgraBlob->GetBufferSize(),
        nullptr, &m_bgraPixelShader);
    if (FAILED(hr)) return hr;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MinLOD         = 0.0f;
    sampler.MaxLOD         = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&sampler, &m_sampler);
    if (FAILED(hr)) return hr;

    // Full-screen quad: culling must not depend on vertex winding.
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.FrontCounterClockwise = FALSE;
    rasterizer.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(&rasterizer, &m_rasterizer);
    if (FAILED(hr)) return hr;

    return S_OK;
}

HRESULT D3D11Renderer::Resize(UINT width, UINT height) {
    if (!m_swapChain) {
        return E_NOT_VALID_STATE;
    }
    if (width == 0 || height == 0) {
        return S_OK;
    }
    if (width == m_backBufferWidth && height == m_backBufferHeight) {
        return S_OK;
    }

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        Logger::Global().Warn("D3D11Renderer",
            std::format("ResizeBuffers failed: {}", HResultToString(hr)));
        // Try to keep rendering with the old target if possible.
        CreateRenderTarget();
        return hr;
    }

    m_backBufferWidth  = width;
    m_backBufferHeight = height;

    hr = CreateRenderTarget();
    if (FAILED(hr)) {
        Logger::Global().Error("D3D11Renderer",
            std::format("Render target recreation failed: {}",
                HResultToString(hr)));
    }
    return hr;
}

HRESULT D3D11Renderer::EnsureTextures(const Frame& frame) {
    const UINT width  = frame.width;
    const UINT height = frame.height;
    if (width == 0 || height == 0) {
        return E_INVALIDARG;
    }

    if (frame.format == m_textureFormat &&
        width == m_textureWidth && height == m_textureHeight) {
        return S_OK;
    }

    m_viewY.Reset();
    m_textureY.Reset();
    m_viewUV.Reset();
    m_textureUV.Reset();
    m_viewBgra.Reset();
    m_textureBgra.Reset();

    HRESULT hr = S_OK;
    if (frame.format == FrameFormat::NV12) {
        hr = CreateDynamicTexture(
            m_device.Get(), width, height, DXGI_FORMAT_R8_UNORM,
            &m_textureY, &m_viewY);
        if (FAILED(hr)) return hr;

        hr = CreateDynamicTexture(
            m_device.Get(), (width + 1) / 2, (height + 1) / 2,
            DXGI_FORMAT_R8G8_UNORM, &m_textureUV, &m_viewUV);
        if (FAILED(hr)) return hr;
    } else {
        hr = CreateDynamicTexture(
            m_device.Get(), width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
            &m_textureBgra, &m_viewBgra);
        if (FAILED(hr)) return hr;
    }

    m_textureWidth  = width;
    m_textureHeight = height;
    m_textureFormat = frame.format;
    return S_OK;
}

HRESULT D3D11Renderer::UploadNv12(const Frame& frame) {
    const UINT width  = frame.width;
    const UINT height = frame.height;
    const UINT stride = std::max<UINT>(frame.stride, width);

    auto mapAndCopy = [&](ID3D11Texture2D* texture,
                          UINT rows,
                          UINT rowBytes,
                          size_t sourceOffset,
                          size_t sourceStride) -> HRESULT {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = m_context->Map(
            texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return hr;

        const auto* src = frame.data.data();
        for (UINT row = 0; row < rows; ++row) {
            const size_t offset = sourceOffset + row * sourceStride;
            if (offset >= frame.data.size()) break;

            const size_t available = frame.data.size() - offset;
            const size_t toCopy = std::min<size_t>(
                { static_cast<size_t>(rowBytes),
                  static_cast<size_t>(mapped.RowPitch),
                  available });

            std::memcpy(
                static_cast<uint8_t*>(mapped.pData) + row * mapped.RowPitch,
                src + offset, toCopy);
        }

        m_context->Unmap(texture, 0);
        return S_OK;
    };

    // Luma plane
    HRESULT hr = mapAndCopy(
        m_textureY.Get(), height, width, 0, stride);
    if (FAILED(hr)) return hr;

    // Interleaved chroma plane starts after the luma plane.
    const UINT chromaRows   = (height + 1) / 2;
    const UINT chromaRowBytes = ((width + 1) / 2) * 2;
    hr = mapAndCopy(
        m_textureUV.Get(), chromaRows, chromaRowBytes,
        static_cast<size_t>(stride) * height, stride);
    return hr;
}

HRESULT D3D11Renderer::UploadBgra(const Frame& frame) {
    const UINT width  = frame.width;
    const UINT height = frame.height;
    const UINT stride = std::max<UINT>(frame.stride, width * 4);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(
        m_textureBgra.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return hr;

    const UINT rowBytes = width * 4;
    for (UINT row = 0; row < height; ++row) {
        const size_t offset = static_cast<size_t>(row) * stride;
        if (offset >= frame.data.size()) break;

        const size_t available = frame.data.size() - offset;
        const size_t toCopy = std::min<size_t>(
            { static_cast<size_t>(rowBytes),
              static_cast<size_t>(mapped.RowPitch),
              available });

        std::memcpy(
            static_cast<uint8_t*>(mapped.pData) + row * mapped.RowPitch,
            frame.data.data() + offset, toCopy);
    }

    m_context->Unmap(m_textureBgra.Get(), 0);
    return S_OK;
}

HRESULT D3D11Renderer::Render(const Frame& frame) {
    if (!m_swapChain || !m_renderTarget) {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = EnsureTextures(frame);
    if (FAILED(hr)) {
        Logger::Global().Warn("D3D11Renderer",
            std::format("Texture setup failed: {}", HResultToString(hr)));
        return hr;
    }

    if (frame.format == FrameFormat::NV12) {
        hr = UploadNv12(frame);
    } else {
        hr = UploadBgra(frame);
    }
    if (FAILED(hr)) {
        Logger::Global().Warn("D3D11Renderer",
            std::format("Texture upload failed: {}", HResultToString(hr)));
        return hr;
    }

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clearColor);
    m_context->OMSetRenderTargets(1, m_renderTarget.GetAddressOf(), nullptr);

    // Letterbox: preserve the frame aspect ratio inside the client area.
    const float windowW = static_cast<float>(m_backBufferWidth);
    const float windowH = static_cast<float>(m_backBufferHeight);
    const float frameAspect = static_cast<float>(frame.width) /
                              static_cast<float>(frame.height);
    const float windowAspect = windowH > 0.0f ? windowW / windowH : 1.0f;

    D3D11_VIEWPORT viewport{};
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    if (frameAspect >= windowAspect) {
        viewport.Width     = windowW;
        viewport.Height    = windowW / frameAspect;
        viewport.TopLeftX  = 0.0f;
        viewport.TopLeftY  = (windowH - viewport.Height) * 0.5f;
    } else {
        viewport.Width     = windowH * frameAspect;
        viewport.Height    = windowH;
        viewport.TopLeftX  = (windowW - viewport.Width) * 0.5f;
        viewport.TopLeftY  = 0.0f;
    }
    m_context->RSSetViewports(1, &viewport);

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_context->RSSetState(m_rasterizer.Get());
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);

    ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
    if (frame.format == FrameFormat::NV12) {
        views[0] = m_viewY.Get();
        views[1] = m_viewUV.Get();
        m_context->PSSetShader(m_nv12PixelShader.Get(), nullptr, 0);
    } else {
        views[0] = m_viewBgra.Get();
        m_context->PSSetShader(m_bgraPixelShader.Get(), nullptr, 0);
    }
    m_context->PSSetShaderResources(0, 2, views);

    ID3D11SamplerState* samplers[] = { m_sampler.Get() };
    m_context->PSSetSamplers(0, 1, samplers);

    m_context->Draw(4, 0);
    return S_OK;
}

HRESULT D3D11Renderer::Present() {
    if (!m_swapChain) {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = m_swapChain->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Logger::Global().Error("D3D11Renderer",
            std::format("Graphics device lost: {}", HResultToString(hr)));
        return hr;
    }
    return SUCCEEDED(hr) ? S_OK : hr;
}

HRESULT D3D11Renderer::ReadBackPixels(
    std::vector<uint8_t>& pixels, UINT& width, UINT& height) {

    if (!m_swapChain) {
        return E_NOT_VALID_STATE;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    desc.Usage          = D3D11_USAGE_STAGING;
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags      = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = m_device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) return hr;

    m_context->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;

    width  = desc.Width;
    height = desc.Height;
    pixels.resize(static_cast<size_t>(width) * height * 4);

    const UINT rowBytes = width * 4;
    for (UINT row = 0; row < height; ++row) {
        std::memcpy(
            pixels.data() + static_cast<size_t>(row) * rowBytes,
            static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch,
            rowBytes);
    }

    m_context->Unmap(staging.Get(), 0);
    return S_OK;
}

void D3D11Renderer::Shutdown() {
    // Unbind resources before releasing them.
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }

    m_viewY.Reset();
    m_textureY.Reset();
    m_viewUV.Reset();
    m_textureUV.Reset();
    m_viewBgra.Reset();
    m_textureBgra.Reset();

    m_sampler.Reset();
    m_rasterizer.Reset();
    m_bgraPixelShader.Reset();
    m_nv12PixelShader.Reset();
    m_vertexShader.Reset();

    m_renderTarget.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_textureWidth  = 0;
    m_textureHeight = 0;
    m_backBufferWidth  = 0;
    m_backBufferHeight = 0;
}

} // namespace frameforge
