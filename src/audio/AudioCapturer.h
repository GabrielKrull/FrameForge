#pragma once

#include "audio/AudioTypes.h"
#include "core/common.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace frameforge {

class AudioCapturer {
public:
    AudioCapturer();
    ~AudioCapturer();

    AudioCapturer(const AudioCapturer&) = delete;
    AudioCapturer& operator=(const AudioCapturer&) = delete;

    // Initialize capturer with a specific audio endpoint ID or empty for default
    HRESULT Initialize(const std::string& endpointId = "");

    // Start background capture thread with event-driven callback
    HRESULT Start(AudioPacketCallback callback);

    // Stop capture thread and halt audio client
    void Stop();

    // Release all resources
    void Close();

    bool IsActive() const { return m_running.load(std::memory_order_relaxed); }
    const AudioFormat& Format() const { return m_format; }
    const std::string& DeviceName() const { return m_deviceName; }

private:
    void CaptureThreadFunc();

    ComPtr<IMMDevice>           m_device;
    ComPtr<IAudioClient>        m_audioClient;
    ComPtr<IAudioCaptureClient> m_captureClient;

    WAVEFORMATEX*               m_pMixFormat = nullptr;
    AudioFormat                 m_format;
    std::string                 m_deviceName;

    HANDLE                      m_wakeEvent = nullptr;
    HANDLE                      m_stopEvent = nullptr;

    std::atomic<bool>           m_running = false;
    AudioPacketCallback         m_callback;
    std::thread                 m_thread;
};

} // namespace frameforge

