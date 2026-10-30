#include "audio/AudioRenderer.h"
#include "audio/AudioFormatUtils.h"
#include "diagnostics/Logger.h"

#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <propvarutil.h>

#include <algorithm>
#include <format>

namespace frameforge {

AudioRenderer::AudioRenderer() = default;

AudioRenderer::~AudioRenderer() {
    Close();
}

HRESULT AudioRenderer::Initialize(
    const AudioFormat& inputFormat,
    const std::string& endpointId) {

    Close();
    m_inputFormat = inputFormat;

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));

    if (FAILED(hr)) {
        Logger::Global().Error("AudioRenderer",
            std::format("CoCreateInstance(MMDeviceEnumerator) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    if (endpointId.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(
            eRender, eMultimedia, &m_device);
        if (FAILED(hr)) {
            Logger::Global().Warn("AudioRenderer",
                std::format("GetDefaultAudioEndpoint(eRender) failed: {}",
                    HResultToString(hr)));
            return hr;
        }
    } else {
        std::wstring wideId = Utf8ToWide(endpointId);
        hr = enumerator->GetDevice(wideId.c_str(), &m_device);
        if (FAILED(hr)) {
            Logger::Global().Error("AudioRenderer",
                std::format("GetDevice(\"{}\") failed: {}",
                    endpointId, HResultToString(hr)));
            return hr;
        }
    }

    // Friendly name
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(m_device->OpenPropertyStore(STGM_READ, &props)) && props) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var))) {
            if (var.vt == VT_LPWSTR && var.pwszVal) {
                m_deviceName = WideToUtf8(var.pwszVal);
            }
        }
        PropVariantClear(&var);
    }

    hr = m_device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        &m_audioClient);

    if (FAILED(hr)) {
        Logger::Global().Error("AudioRenderer",
            std::format("Activate(IAudioClient) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    hr = m_audioClient->GetMixFormat(&m_pMixFormat);
    if (FAILED(hr) || !m_pMixFormat) {
        Logger::Global().Error("AudioRenderer",
            std::format("GetMixFormat failed: {}", HResultToString(hr)));
        return hr;
    }

    m_format = WaveFormatToAudioFormat(m_pMixFormat);

    // 40 ms buffer duration (400,000 * 100ns units)
    constexpr REFERENCE_TIME kBufferDuration100ns = 400'000;

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        kBufferDuration100ns,
        0,
        m_pMixFormat,
        nullptr);

    if (FAILED(hr)) {
        Logger::Global().Error("AudioRenderer",
            std::format("IAudioClient::Initialize failed: {}",
                HResultToString(hr)));
        return hr;
    }

    hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        Logger::Global().Error("AudioRenderer",
            std::format("GetBufferSize failed: {}", HResultToString(hr)));
        return hr;
    }

    hr = m_audioClient->GetService(
        __uuidof(IAudioRenderClient),
        &m_renderClient);

    if (FAILED(hr)) {
        Logger::Global().Error("AudioRenderer",
            std::format("GetService(IAudioRenderClient) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    Logger::Global().Info("AudioRenderer",
        std::format("Audio monitor initialized: \"{}\" [{}] buffer={} frames",
            m_deviceName, m_format.ToString(), m_bufferFrameCount));

    return S_OK;
}

HRESULT AudioRenderer::Start() {
    if (!m_audioClient || !m_renderClient) {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        Logger::Global().Error("AudioRenderer",
            std::format("IAudioClient::Start failed: {}", HResultToString(hr)));
        return hr;
    }

    m_active.store(true, std::memory_order_relaxed);
    Logger::Global().Info("AudioRenderer", "Audio monitor started");
    return S_OK;
}

void AudioRenderer::Stop() {
    if (!m_active.exchange(false, std::memory_order_relaxed)) {
        return;
    }

    if (m_audioClient) {
        m_audioClient->Stop();
    }
    Logger::Global().Info("AudioRenderer", "Audio monitor stopped");
}

void AudioRenderer::Close() {
    Stop();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_renderClient.Reset();
    m_audioClient.Reset();
    m_device.Reset();

    if (m_pMixFormat) {
        CoTaskMemFree(m_pMixFormat);
        m_pMixFormat = nullptr;
    }

    m_format = {};
    m_inputFormat = {};
    m_deviceName.clear();
    m_bufferFrameCount = 0;
}

void AudioRenderer::RenderPacket(const AudioPacket& packet) {
    if (!m_active.load(std::memory_order_relaxed) || !m_renderClient) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderClient || !m_audioClient) return;

    UINT32 padding = 0;
    HRESULT hr = m_audioClient->GetCurrentPadding(&padding);
    if (FAILED(hr)) return;

    if (padding >= m_bufferFrameCount) {
        // Buffer full: drop packet to avoid latency buildup
        return;
    }

    const UINT32 availableFrames = m_bufferFrameCount - padding;
    if (packet.frames > availableFrames) {
        // Drop on congestion: preserves live latency
        return;
    }

    BYTE* pBuffer = nullptr;
    hr = m_renderClient->GetBuffer(packet.frames, &pBuffer);
    if (FAILED(hr) || !pBuffer) return;

    const uint32_t bytesToWrite = packet.frames * m_format.BlockAlign();
    const uint32_t bytesInPacket = static_cast<uint32_t>(packet.data.size());
    const uint32_t copyBytes = std::min(bytesToWrite, bytesInPacket);

    if (packet.IsSilent() || copyBytes == 0) {
        std::memset(pBuffer, 0, bytesToWrite);
    } else {
        std::memcpy(pBuffer, packet.data.data(), copyBytes);
        if (copyBytes < bytesToWrite) {
            std::memset(pBuffer + copyBytes, 0, bytesToWrite - copyBytes);
        }
    }

    DWORD flags = packet.IsSilent() ? AUDCLNT_BUFFERFLAGS_SILENT : 0;
    m_renderClient->ReleaseBuffer(packet.frames, flags);
}

} // namespace frameforge

