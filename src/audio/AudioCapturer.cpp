#include "audio/AudioCapturer.h"
#include "audio/AudioFormatUtils.h"
#include "diagnostics/Logger.h"

#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <propvarutil.h>

#include <format>
#include <utility>

namespace frameforge {

AudioCapturer::AudioCapturer() = default;

AudioCapturer::~AudioCapturer() {
    Close();
}

HRESULT AudioCapturer::Initialize(const std::string& endpointId) {
    Close();

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));

    if (FAILED(hr)) {
        Logger::Global().Error("AudioCapturer",
            std::format("CoCreateInstance(MMDeviceEnumerator) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    if (endpointId.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(
            eCapture, eMultimedia, &m_device);
        if (FAILED(hr)) {
            Logger::Global().Warn("AudioCapturer",
                std::format("GetDefaultAudioEndpoint failed: {}",
                    HResultToString(hr)));
            return hr;
        }
    } else {
        std::wstring wideId = Utf8ToWide(endpointId);
        hr = enumerator->GetDevice(wideId.c_str(), &m_device);
        if (FAILED(hr)) {
            Logger::Global().Error("AudioCapturer",
                std::format("GetDevice(\"{}\") failed: {}",
                    endpointId, HResultToString(hr)));
            return hr;
        }
    }

    // Retrieve friendly name
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
        Logger::Global().Error("AudioCapturer",
            std::format("Activate(IAudioClient) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    hr = m_audioClient->GetMixFormat(&m_pMixFormat);
    if (FAILED(hr) || !m_pMixFormat) {
        Logger::Global().Error("AudioCapturer",
            std::format("GetMixFormat failed: {}", HResultToString(hr)));
        return hr;
    }

    m_format = WaveFormatToAudioFormat(m_pMixFormat);

    // 20 ms buffer duration (200,000 * 100ns units)
    constexpr REFERENCE_TIME kBufferDuration100ns = 200'000;

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        kBufferDuration100ns,
        0,
        m_pMixFormat,
        nullptr);

    if (FAILED(hr)) {
        Logger::Global().Error("AudioCapturer",
            std::format("IAudioClient::Initialize failed: {}",
                HResultToString(hr)));
        return hr;
    }

    m_wakeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_wakeEvent) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Logger::Global().Error("AudioCapturer", "CreateEvent(wakeEvent) failed");
        return hr;
    }

    hr = m_audioClient->SetEventHandle(m_wakeEvent);
    if (FAILED(hr)) {
        Logger::Global().Error("AudioCapturer",
            std::format("SetEventHandle failed: {}", HResultToString(hr)));
        return hr;
    }

    hr = m_audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        &m_captureClient);

    if (FAILED(hr)) {
        Logger::Global().Error("AudioCapturer",
            std::format("GetService(IAudioCaptureClient) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    Logger::Global().Info("AudioCapturer",
        std::format("Audio capturer initialized: \"{}\" [{}]",
            m_deviceName, m_format.ToString()));

    return S_OK;
}

HRESULT AudioCapturer::Start(AudioPacketCallback callback) {
    if (!m_audioClient || !m_captureClient) {
        return E_NOT_VALID_STATE;
    }
    if (m_running.load(std::memory_order_relaxed)) {
        return S_FALSE;
    }

    m_callback = std::move(callback);
    m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        Logger::Global().Error("AudioCapturer",
            std::format("IAudioClient::Start failed: {}", HResultToString(hr)));
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
        return hr;
    }

    m_running.store(true, std::memory_order_relaxed);
    m_thread = std::thread(&AudioCapturer::CaptureThreadFunc, this);

    Logger::Global().Info("AudioCapturer", "Audio capture started");
    return S_OK;
}

void AudioCapturer::CaptureThreadFunc() {
    HANDLE waitHandles[2] = { m_wakeEvent, m_stopEvent };

    while (m_running.load(std::memory_order_relaxed)) {
        DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);
        if (wait == WAIT_OBJECT_0) {
            // Wake event: read all available packets
            for (;;) {
                UINT32 packetLength = 0;
                HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr) || packetLength == 0) {
                    break;
                }

                BYTE* pData = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;
                UINT64 devPos = 0;
                UINT64 qpcPos = 0;

                hr = m_captureClient->GetBuffer(
                    &pData, &numFrames, &flags, &devPos, &qpcPos);

                if (FAILED(hr)) {
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        Logger::Global().Error("AudioCapturer",
                            "Audio device disconnected/invalidated");
                        m_running.store(false, std::memory_order_relaxed);
                    }
                    break;
                }

                if (qpcPos == 0) {
                    LARGE_INTEGER now{};
                    QueryPerformanceCounter(&now);
                    qpcPos = static_cast<UINT64>(now.QuadPart);
                }

                if (m_callback && numFrames > 0) {
                    AudioPacket packet;
                    packet.frames = numFrames;
                    packet.qpcTimestamp = static_cast<int64_t>(qpcPos);
                    packet.devicePosition = devPos;
                    packet.flags = flags;

                    const uint32_t bytes = numFrames * m_format.BlockAlign();
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !pData) {
                        packet.data.resize(bytes, 0);
                    } else {
                        packet.data.assign(pData, pData + bytes);
                    }

                    m_callback(std::move(packet));
                }

                m_captureClient->ReleaseBuffer(numFrames);
            }
        } else if (wait == WAIT_OBJECT_0 + 1) {
            // Stop event
            break;
        } else if (wait == WAIT_TIMEOUT) {
            Logger::Global().Debug("AudioCapturer", "Audio wait timeout (no samples in 2s)");
        } else {
            break;
        }
    }
}

void AudioCapturer::Stop() {
    if (!m_running.exchange(false, std::memory_order_relaxed)) {
        return;
    }

    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_audioClient) {
        m_audioClient->Stop();
    }

    Logger::Global().Info("AudioCapturer", "Audio capture stopped");
}

void AudioCapturer::Close() {
    Stop();

    if (m_wakeEvent) {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }

    m_captureClient.Reset();
    m_audioClient.Reset();
    m_device.Reset();

    if (m_pMixFormat) {
        CoTaskMemFree(m_pMixFormat);
        m_pMixFormat = nullptr;
    }

    m_format = {};
    m_deviceName.clear();
}

} // namespace frameforge

